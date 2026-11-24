#include "pch.h"

#include "game/news.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from news.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.12).
namespace news {

    bool enabled = false;
    bool intelligence = false;

} // namespace news
