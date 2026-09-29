#include "audio_gc.h"

#include <asndlib.h>
#include <malloc.h>
#include <ogc/cache.h>

#include <cstring>
#include <string>

#include "System/System.h"

namespace audio_gc {

namespace {

const char* const kRoot = "BJ2GC/Scripts/Data/snd_";
constexpr int kFirstVoice = 8, kVoices = 7;  // 15 is the music (music_gc)
constexpr int kVolume = 200;  // of 255

struct Clip {
    void* samples = nullptr;
    uint32_t bytes = 0;
    int rate = 0;
};

enum Id {
    kSelect, kBad, kMatch, kMatchBig, kCombo2, kCombo3, kCombo4, kCombo5, kCombo6, kCombo7, kLand, kExplode,
    kHyperMade, kZapStart, kZap, kLevelUp, kGo, kNoMoves, kExcellent, kIncredible, kGetReady, kGood, kMultishot,
    kWhirlpool, kElectroPath, kCount
};
const char* const kNames[kCount] = {"select", "bad2", "gotset2", "gotsetbig2", "combo22", "combo32", "combo42",
                                    "combo52", "combo62", "combo72", "gemongem2", "explode2", "hypergem_creation",
                                    "electro_start", "electro_explode", "level_complete", "go", "no_more_moves",
                                    "excellent1", "incredible", "get_ready", "good", "multishot", "whirlpool1",
                                    "electro_path"};

Clip g_clips[kCount];
int g_next = 0;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

bool LoadClip(Clip& c, const char* name) {
    char* data = nullptr;
    uint32_t size = 0;
    SYS_AcquireFileData((std::string(kRoot) + name + ".pcm").c_str(), true, 0, data, size);
    if (!data) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data);
    uint32_t bytes = size >= 32 ? be32(d + 8) : 0;
    if (size < 32 || std::memcmp(d, "BJSD", 4) != 0 || bytes == 0 || bytes > size - 32) {
        SYS_ReleaseFileData(data);
        return false;
    }
    c.samples = memalign(32, bytes);
    if (c.samples) {
        std::memcpy(c.samples, d + 32, bytes);
        DCFlushRange(c.samples, bytes);
        c.bytes = bytes;
        c.rate = int(be32(d + 4));
    }
    SYS_ReleaseFileData(data);
    return c.samples != nullptr;
}

void PlayClip(int id) {
    const Clip& c = g_clips[id];
    if (!c.samples) return;
    int voice = kFirstVoice + g_next;
    g_next = (g_next + 1) % kVoices;
    ASND_StopVoice(voice);
    ASND_SetVoice(voice, VOICE_MONO_16BIT, c.rate, 0, c.samples, int(c.bytes), kVolume, kVolume, nullptr);
}

}  // namespace

bool Load() {
    bool all = true;
    for (int i = 0; i < kCount; i++) all &= LoadClip(g_clips[i], kNames[i]);
    return all;
}

void Free() {
    for (int i = kFirstVoice; i < kFirstVoice + kVoices; i++) ASND_StopVoice(i);
    for (Clip& c : g_clips) {
        free(c.samples);
        c = Clip();
    }
}

void Play(bj2::Sound sound, int level) {
    using bj2::Sound;
    switch (sound) {
    case Sound::Go: PlayClip(kGo); break;
    case Sound::Bad: PlayClip(kBad); break;
    case Sound::Match: PlayClip(kMatch); break;
    case Sound::MatchBig: PlayClip(kMatchBig); break;
    case Sound::Combo: PlayClip(kCombo2 + (level < 2 ? 0 : level > 7 ? 5 : level - 2)); break;
    case Sound::Land: PlayClip(kLand); break;
    case Sound::Explode: PlayClip(kExplode); break;
    case Sound::PowerMade: PlayClip(kMultishot); break;
    case Sound::HyperMade: PlayClip(kHyperMade); break;
    case Sound::Zap: PlayClip(kZapStart); break;
    case Sound::ZapGem: PlayClip(kZap); break;
    case Sound::Bolt: PlayClip(kElectroPath); break;
    case Sound::LevelUp: PlayClip(kLevelUp); break;
    case Sound::Whirlpool: PlayClip(kWhirlpool); break;
    case Sound::NoMoves: PlayClip(kNoMoves); break;
    case Sound::Good: PlayClip(kGood); break;
    case Sound::Excellent: PlayClip(kExcellent); break;
    case Sound::Incredible: PlayClip(kIncredible); break;
    case Sound::GetReady: PlayClip(kGetReady); break;
    }
}

void PlaySelect() { PlayClip(kSelect); }

}  // namespace audio_gc
