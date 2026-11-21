#include "pch.h"

#include "game/council_ai.h"

// Configuration global bound from specs/config/main.json5 (gameplay council_without_humans). Kept
// apart from council.cpp so every target that links the generated config bindings gets it.
namespace council {
    bool meetings_without_humans = false;
}
