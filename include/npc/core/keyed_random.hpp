#pragma once

#include <cstdint>

namespace npc {

// Independent draws: adding an unrelated event never advances another choice.
class KeyedRandom {
public:
    static uint64_t mix(uint64_t value) {
        value += UINT64_C(0x9e3779b97f4a7c15);
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }

    static uint64_t combine(uint64_t seed, uint64_t value) {
        return mix(seed ^ mix(value));
    }

    // Strictly inside (0, 1), including after conversion to double.
    static double unit(uint64_t seed, uint64_t key) {
        return (static_cast<double>(combine(seed, key) >> 12) + 0.5)
             / 4503599627370496.0;
    }
};

} // namespace npc
