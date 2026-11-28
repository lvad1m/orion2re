#pragma once

#include <stddef.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game's own new texts in the interface language (setting "interface language", text_language.h):
// data/base/texts/<lang>.json5, nested objects whose keys are flattened with dots
// ("choices.incidents.painless.title"). A mod adds or fixes a language with its own data/texts/<lang>.json5
// (a key of a higher layer wins). Strings are kept in the game's bytes: ASCII for English, CP866 for Russian.
namespace texts {
    // The text of `key` in the current language, else in English, else the key itself (a log line once
    // per missing key). Loads the files on first use when Load was not called. Never nullptr; the pointer
    // stays valid until the next Load_Source or Reset.
    const char* Get(const char* key);

    // The text of `key` with the named placeholders {name} replaced by the values. No printf formats come
    // from data files. A placeholder that is not among the arguments stays as written; an argument the
    // text does not mention is ignored; an empty value leaves the placeholder empty. Translations may
    // reorder placeholders. A plural placeholder {name|form1|form2|form3} gives the integer value, a space and
    // the form for it (Russian: 1, 2-4, 5+ rules, three forms; other languages: 1 or not, two forms; fewer forms:
    // the last; a non-integer value: the last form). The result is cut to fit `size` (always terminated).
    void Format(char* out, size_t size, const char* key, std::initializer_list<std::pair<const char*, std::string>> args);

    // The same texts as UTF-8, for the windows drawn with a Unicode font (the mentat panel): the raw string of the
    // current language (English fallback, then the key). Same lifetime rules as Get.
    const char* Get_Utf8(const char* key);

    using t_args = std::vector<std::pair<std::string, std::string>>;
    // Format with a list of named values: the game's bytes (utf8 false, as Get) or UTF-8 (utf8 true, as Get_Utf8).
    // The values are inserted as given. The rules of Format apply.
    std::string Expand(const char* key, const t_args& args, bool utf8);
    // Format for UTF-8 (the template from Get_Utf8).
    void Format_Utf8(char* out, size_t size, const char* key, std::initializer_list<std::pair<const char*, std::string>> args);

    // Reads texts/en.json5 and texts/ru.json5 through the VFS (every layer, a mod over the base), once.
    // Call at startup after the configuration; a language change needs a restart. A broken file logs one
    // warning and is skipped. Never fails.
    void Load();

    // Merges one JSON5 text (UTF-8) of `language` (text_language::e_language) into the table; keys of this
    // text replace the same keys already there. False (and one log line) when the text is not a JSON5
    // object, nothing is merged then. Marks the module loaded.
    bool Load_Source(int language, std::string_view json5, const char* origin = "");

    // Forgets everything, including the "missing key" log memory (tests).
    void Reset();
} // namespace texts
