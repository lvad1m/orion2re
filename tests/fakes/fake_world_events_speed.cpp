#include "pch.h"

#include "game/world_events.h"

// New events are not under test in the ship initialisation test.
namespace world_events {
    int32_t Speed_Bonus(int16_t) { return 0; }
    int32_t Scaled_Speed(int32_t speed) { return speed; }
} // namespace world_events
