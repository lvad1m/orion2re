#include "pch.h"

#include "game/text_language.h"

// Configuration globals bound from specs/config/main.json5 (namespace "interface"), kept apart from
// text_language.cpp like the other *_config.cpp files.
namespace text_language {

    uint8_t language = LANGUAGE_EN;

} // namespace text_language
