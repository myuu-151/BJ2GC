// The music on the GameCube: music_main.ogm (tools/make_data.py, Ogg Vorbis
// with a small header) streamed from the disc and looped. A thread reads the
// file ahead into a ring; the main thread decodes it (Update, each frame)
// into buffers it queues on ASND voice 15. Octave's own streams work the same
// way (Audio_Dolphin.cpp); this one only needs no Octave asset.
#pragma once

namespace music_gc {

bool Start();   // false if the music is missing or won't open
void Update();  // each frame: decode what the voice will need next

}  // namespace music_gc
