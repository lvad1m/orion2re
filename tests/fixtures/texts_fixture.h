#pragma once

#include "game/texts.h"
#include "game/text_language.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace test {
    // data/base/texts of the source tree (this header is at tests/fixtures).
    inline std::filesystem::path Base_Texts_Dir() {
        const std::filesystem::path tail = std::filesystem::path("data") / "base" / "texts";
        std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / tail;
        if (std::filesystem::exists(dir)) return dir;
        // __FILE__ may be relative to the build folder: walk up from the working directory instead.
        for (std::filesystem::path up = std::filesystem::current_path(); up.has_parent_path(); up = up.parent_path()) {
            if (std::filesystem::exists(up / tail)) return up / tail;
            if (up == up.parent_path()) break;
        }
        return dir;
    }

    inline std::string Read_Base_Text_File(const char* name) {
        std::ifstream in(Base_Texts_Dir() / name, std::ios::binary);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    // Loads the shipped en.json5 and ru.json5 into the texts module (the VFS has no base data in unit tests).
    inline void Load_Base_Texts() {
        texts::Reset();
        texts::Load_Source(text_language::LANGUAGE_EN, Read_Base_Text_File("en.json5"), "en.json5");
        texts::Load_Source(text_language::LANGUAGE_RU, Read_Base_Text_File("ru.json5"), "ru.json5");
    }
} // namespace test
