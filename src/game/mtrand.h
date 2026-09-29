// Sexy::MTRand, the framework's Mersenne Twister (MT19937), as the game has
// it: SRand 0x40b4f0 (a seed of 0 is 4357), Next 0x40b550 (31 bits). The
// game has a global one (Sexy::Rand, 0x411020: effects, animation, the
// seed of each new board) and one in each board (Board+0xc20: the gems).
#pragma once

#include <cstdint>

namespace bj2 {

class MTRand {
public:
    static constexpr int kN = 624;

    MTRand() { SRand(4357); }
    explicit MTRand(uint32_t seed) { SRand(seed); }

    void SRand(uint32_t seed);
    uint32_t Next();  // 0 .. 0x7fffffff

    // The state as a saved game keeps it (0x40b4a0: 624 words).
    const uint32_t* State() const { return mt_; }
    int Index() const { return mti_; }
    void SetState(const uint32_t* words, int index) {
        for (int i = 0; i < kN; i++) mt_[i] = words[i];
        mti_ = index;
    }

private:
    uint32_t mt_[kN];
    int mti_ = kN + 1;
};

}  // namespace bj2
