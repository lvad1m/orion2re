#include "pch.h"

#include "game/mentat.h"

// Configuration globals bound from specs/config/main.json5 (group interface, namespace "interface"), kept apart from
// mentat.cpp like the other *_config.cpp files. Local to this machine: not saved, not synchronized.
namespace mentat {

    uint8_t mode = MODE_OFF;
    uint8_t detail = DETAIL_BRIEF;
    uint8_t open_when = OPEN_CRITICAL;
    int32_t period = 5;
    bool alerts = true;

} // namespace mentat
