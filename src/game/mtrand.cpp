#include "game/mtrand.h"

namespace bj2 {

namespace {
constexpr int kM = 397;
constexpr uint32_t kMatrixA = 0x9908b0df;
constexpr uint32_t kUpper = 0x80000000;
constexpr uint32_t kLower = 0x7fffffff;
}  // namespace

void MTRand::SRand(uint32_t seed) {
    if (seed == 0) seed = 4357;
    mt_[0] = seed;
    for (mti_ = 1; mti_ < kN; mti_++)
        mt_[mti_] = 1812433253u * (mt_[mti_ - 1] ^ (mt_[mti_ - 1] >> 30)) + uint32_t(mti_);
}

uint32_t MTRand::Next() {
    static const uint32_t mag01[2] = {0, kMatrixA};
    if (mti_ >= kN) {
        int k = 0;
        for (; k < kN - kM; k++) {
            uint32_t y = (mt_[k] & kUpper) | (mt_[k + 1] & kLower);
            mt_[k] = mt_[k + kM] ^ (y >> 1) ^ mag01[y & 1];
        }
        for (; k < kN - 1; k++) {
            uint32_t y = (mt_[k] & kUpper) | (mt_[k + 1] & kLower);
            mt_[k] = mt_[k + (kM - kN)] ^ (y >> 1) ^ mag01[y & 1];
        }
        uint32_t y = (mt_[kN - 1] & kUpper) | (mt_[0] & kLower);
        mt_[kN - 1] = mt_[kM - 1] ^ (y >> 1) ^ mag01[y & 1];
        mti_ = 0;
    }
    uint32_t y = mt_[mti_++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= y >> 18;
    return y & 0x7fffffff;
}

}  // namespace bj2
