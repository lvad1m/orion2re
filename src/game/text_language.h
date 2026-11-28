#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>

// Interface text language (setting "interface language"). The game's own texts can come from the
// Russian twin of each original LBX file (the fan translation of the 1.31 Russian release), when the
// twin is in the game folder. Buttons and pictures stay English; only text files and fonts are mapped.
namespace text_language {
    enum e_language : uint8_t {
        LANGUAGE_EN = 0, // the original files, nothing is mapped
        LANGUAGE_RU = 1, // the Russian twin of each original file when it exists
    };

    // Configuration global (bound from specs/config/main.json5).
    extern uint8_t language;

    // The Russian twin of an original LBX name ("estrings.lbx" -> "estrruss.lbx"), case-insensitive,
    // nullptr when the file has none. Original_Name is the reverse lookup. Pure table queries.
    const char* Twin_Name(const char* original);
    const char* Original_Name(const char* twin);

    // Hooks the name mapping into the VFS: with LANGUAGE_RU every table name whose twin exists through
    // the VFS opens the twin (logged once per file); anything else opens the original silently. With
    // LANGUAGE_EN the hook is removed, so nothing changes at all. Call at startup after the config.
    void Install(uint8_t language_value);

    // Stops using the twin of `original` for the rest of the run (a twin whose table is unusable).
    void Disable_Twin(const char* original);

    // Forgets what Install logged and Disable_Twin switched off (tests).
    void Reset();

    // CP866 byte of a Unicode code point: Cyrillic (U+0410-U+044F, U+0401, U+0451) and the CP866
    // pseudographics (box drawing and block elements). False for everything else.
    bool Cp866_From_Codepoint(uint32_t codepoint, uint8_t* byte);

    // Decodes the UTF-8 character at text[index]. Returns its length in bytes, 0 when it is malformed.
    size_t Decode_Utf8(std::string_view text, size_t index, uint32_t* codepoint);

    // The game's bytes to UTF-8, for a window that draws UTF-8 (the mentat panel): with LANGUAGE_RU the CP866
    // bytes of Cyrillic (and the pseudographics) become their characters, anything else above 0x7F becomes '?';
    // with LANGUAGE_EN and for ASCII the bytes stay as they are. Pure.
    std::string To_Utf8(std::string_view bytes);

    // UTF-8 to the CP866 bytes of the Russian text files: ASCII as is, Cyrillic and pseudographics to
    // CP866, anything else (and malformed input) to '?'. Returns false when something became '?'.
    bool From_Utf8(std::string_view utf8, std::string* bytes);
} // namespace text_language
