#include "pch.h"

#include "game/advisor.h"

// Configuration globals bound from specs/config/main.json5 (namespace "advisor"). Kept apart
// from advisor.cpp, like the other *_config.cpp files, so every target that links the
// generated config bindings gets them without the rest of the advisor.
namespace advisor {

    bool enabled = false;
    uint8_t journal = JOURNAL_OFF;

} // namespace advisor
