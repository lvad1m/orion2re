#include "pch.h"

#include "game/advisor_apply.h"
#include "game/advisor_intel.h"

namespace test_aidesign {
    // What the faked intel memory reports; tests set and reset these.
    advisor::e_seen_threat seen_threat = advisor::SEEN_THREAT_NONE;
    bool avoid_missiles = false;
} // namespace test_aidesign

namespace advisor {
    // The advisor is off in these tests: ships are designed against the original reference.
    int16_t Design_Reference_Enemy(uint8_t, int16_t fallback) {
        return fallback;
    }

    e_seen_threat Design_Seen_Threat(uint8_t, int16_t) {
        return test_aidesign::seen_threat;
    }

    bool Design_Avoid_Missiles(uint8_t, int16_t) {
        return test_aidesign::avoid_missiles;
    }
} // namespace advisor
