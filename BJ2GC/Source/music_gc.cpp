#include "music_gc.h"

#include <asndlib.h>
#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "System/System.h"
#include "vorbis/vorbisfile.h"

namespace music_gc {

namespace {

const char* const kPath = "BJ2GC/Scripts/Data/music_main.ogm";
constexpr uint32_t kHeader = 32;
constexpr int kVoice = 15;
constexpr int kVolume = 150;  // of 255, under the effects

// The file ahead of the decoder: a ring of kRing bytes the thread fills a
// kChunk at a time. Positions count bytes of the Ogg, on through its loops
// (position p is the Ogg's byte p % size).
constexpr uint32_t kRing = 256 * 1024;
constexpr uint32_t kChunk = 32 * 1024;

// PCM for the voice: one playing, one queued, one being decoded.
constexpr int kBuffers = 3;
constexpr uint32_t kBufferBytes = 16 * 1024;  // 1/8 s of 32 kHz stereo
constexpr uint32_t kSliceBytes = 4 * 1024;    // decoded a frame: twice what a frame plays

uint32_t g_size = 0;           // the Ogg's bytes
uint8_t* g_ring = nullptr;
volatile uint32_t g_written = 0;  // by the thread
volatile uint32_t g_read = 0;     // by the decoder
volatile bool g_failed = false;   // a read failed: the music stops
uint32_t g_pass = 0;              // where this pass through the Ogg began
sem_t g_sem = LWP_SEM_NULL;
lwp_t g_thread = LWP_THREAD_NULL;

OggVorbis_File g_vf;
bool g_open = false, g_started = false;
int g_rate = 0;
uint8_t* g_buf[kBuffers] = {};
uint32_t g_filled = 0;
int g_next = 0;

// Below the main thread (64), as Octave's audio reader: it reads while the
// main thread waits on the GPU and the retrace.
void* Reader(void*) {
    uint8_t* chunk = static_cast<uint8_t*>(memalign(32, kChunk));
    while (chunk && !g_failed) {
        if (g_written - g_read > kRing - kChunk) {
            LWP_SemWait(g_sem);
            continue;
        }
        uint32_t at = g_written % g_size;
        uint32_t n = g_size - at < kChunk ? g_size - at : kChunk;
        if (!SYS_ReadFileRange(kPath, true, kHeader + at, n, reinterpret_cast<char*>(chunk))) {
            g_failed = true;
            break;
        }
        uint32_t to = g_written % kRing;
        uint32_t first = kRing - to < n ? kRing - to : n;
        std::memcpy(g_ring + to, chunk, first);
        std::memcpy(g_ring, chunk + first, n - first);
        g_written = g_written + n;
    }
    free(chunk);
    return nullptr;
}

// vorbisfile's reads, from the ring, to the end of this pass (a loop is a
// new pass, opened again). Waits for the reader if it is behind: at the
// start, and only then if the disc is slow.
size_t RingRead(void* ptr, size_t size, size_t nmemb, void*) {
    uint32_t want = uint32_t(size * nmemb);
    uint32_t left = g_pass + g_size - g_read;
    if (want > left) want = left;
    for (int waited = 0; g_written == g_read && want > 0; waited++) {
        if (g_failed || waited > 2000) return 0;
        usleep(1000);
    }
    uint32_t have = g_written - g_read;
    if (want > have) want = have;
    uint8_t* out = static_cast<uint8_t*>(ptr);
    uint32_t from = g_read % kRing;
    uint32_t first = kRing - from < want ? kRing - from : want;
    std::memcpy(out, g_ring + from, first);
    std::memcpy(out + first, g_ring, want - first);
    g_read = g_read + want;
    LWP_SemPost(g_sem);
    return want / size;
}

bool OpenPass() {
    // No seeking: read straight through. The data source is the ring, but
    // vorbisfile reads nothing from a null one.
    ov_callbacks cb = {RingRead, nullptr, nullptr, nullptr};
    if (ov_open_callbacks(g_ring, &g_vf, nullptr, 0, cb) != 0) return false;
    vorbis_info* info = ov_info(&g_vf, -1);
    if (!info || info->channels != 2) {
        ov_clear(&g_vf);
        return false;
    }
    g_rate = int(info->rate);
    return true;
}

// Decodes up to `want` bytes; at the end of the Ogg, on from its start.
uint32_t Decode(uint8_t* dst, uint32_t want) {
    uint32_t done = 0;
    int errors = 0;
    while (g_open && done < want) {
        int section = 0;
        long n = ov_read(&g_vf, reinterpret_cast<char*>(dst + done), int(want - done), 1, 2, 1, &section);
        if (n > 0) {
            done += uint32_t(n);
        } else if (n == 0) {
            // The end: whatever is left of this pass read past, and the next begins.
            ov_clear(&g_vf);
            static uint8_t skip[1024];
            while (RingRead(skip, 1, sizeof(skip), nullptr) > 0) {}
            g_open = g_read == g_pass + g_size;
            g_pass = g_read;
            if (g_open) g_open = OpenPass();
            if (!g_open) SYS_Report("bj2: music: could not loop\n");
        } else if (++errors > 8) {
            ov_clear(&g_vf);
            g_open = false;
            SYS_Report("bj2: music: decoding failed\n");
        }
    }
    return done;
}

void Callback(s32) {}  // non-null: a voice that runs dry waits for more

}  // namespace

bool Start() {
    char header[kHeader] __attribute__((aligned(32)));
    if (!SYS_ReadFileRange(kPath, true, 0, kHeader, header) || std::memcmp(header, "BJMU", 4) != 0) {
        SYS_Report("bj2: music: no %s\n", kPath);
        return false;
    }
    const uint8_t* h = reinterpret_cast<const uint8_t*>(header);
    g_size = uint32_t(h[4]) << 24 | uint32_t(h[5]) << 16 | uint32_t(h[6]) << 8 | h[7];
    g_ring = static_cast<uint8_t*>(memalign(32, kRing));
    for (uint8_t*& b : g_buf) b = static_cast<uint8_t*>(memalign(32, kBufferBytes));
    if (g_size == 0 || !g_ring || !g_buf[kBuffers - 1]) {
        SYS_Report("bj2: music: %u bytes, or no memory\n", unsigned(g_size));
        return false;
    }
    if (LWP_SemInit(&g_sem, 0, 64) != 0 ||
        LWP_CreateThread(&g_thread, Reader, nullptr, nullptr, 64 * 1024, 50) != 0) {
        SYS_Report("bj2: music: no reader thread\n");
        return false;
    }
    g_open = OpenPass();
    if (!g_open)
        SYS_Report("bj2: music: the Ogg won't open (%u of %u bytes read%s)\n", unsigned(g_read), unsigned(g_written),
                   g_failed ? ", a disc read failed" : "");
    return g_open;
}

void Update() {
    if (!g_open) return;
    uint8_t* buf = g_buf[g_next];
    const bool ready = !g_started || ASND_TestVoiceBufferReady(kVoice) == 1;
    // A slice a frame; all of the buffer if the voice is about to need it.
    uint32_t want = ready ? kBufferBytes - g_filled : kSliceBytes;
    if (want > kBufferBytes - g_filled) want = kBufferBytes - g_filled;
    g_filled += Decode(buf + g_filled, want);
    if (g_filled < kBufferBytes || !ready) return;

    DCFlushRange(buf, kBufferBytes);
    if (!g_started) {
        g_started = ASND_SetVoice(kVoice, VOICE_STEREO_16BIT, g_rate, 0, buf, int(kBufferBytes), kVolume, kVolume,
                                  Callback) == SND_OK;
        if (!g_started) return;
    } else if (ASND_AddVoice(kVoice, buf, int(kBufferBytes)) != SND_OK) {
        return;  // not taken yet: again next frame
    }
    g_next = (g_next + 1) % kBuffers;
    g_filled = 0;
}

}  // namespace music_gc
