#include "pch.h"

#include "game/advisor_apply.h"

namespace advisor {
    // The advisor is off in these tests: ships are designed against the original reference.
    int16_t Design_Reference_Enemy(uint8_t, int16_t fallback) {
        return fallback;
    }
} // namespace advisor
