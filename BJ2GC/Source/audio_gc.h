// The game's sound effects on the GameCube: snd_NAME.pcm (tools/make_data.py,
// 16-bit mono) loaded into memory and played on ASND's voices 8 to 14, of the
// ones Octave's own audio leaves (it isn't used here; 15 is the music).
#pragma once

#include "game/game.h"

namespace audio_gc {

bool Load();  // false if a sound is missing
void Free();
void Play(bj2::Sound sound, int level = 0);
void PlaySelect();  // a gem picked up

}  // namespace audio_gc
