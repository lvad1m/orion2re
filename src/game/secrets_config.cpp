#include "pch.h"

#include "game/secrets.h"

// Configuration globals bound from specs/config/main.json5 (namespace "spy"), kept apart from
// secrets.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.2).
namespace secrets {

    bool enabled = false;
    bool smear = false;
    bool council_kompromat = false;
    bool kompromat_scandal = false;
    bool scandal_breaks_pacts = false;
    int32_t smear_cost = 20;

} // namespace secrets
