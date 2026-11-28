#include "pch.h"

#include <mutex>
#include <set>

#include "text_language.h"
#include "vfs.h"

namespace text_language {
    namespace {
        struct s_twin {
            const char* original;
            const char* twin;
        };

        // Original LBX file -> the Russian twin of the 1.31 Russian release (lowercase).
        constexpr s_twin TWINS[] = {
            {"antarmsg.lbx", "antrmsgr.lbx"},
            {"billtex2.lbx", "billtx2r.lbx"},
            {"billtext.lbx", "billtxtr.lbx"},
            {"councmsg.lbx", "counmsgr.lbx"},
            {"credits.lbx", "crdtsru.lbx"},
            {"diplomse.lbx", "diplomsr.lbx"},
            {"estrings.lbx", "estrruss.lbx"},
            {"eventmsg.lbx", "eventmsr.lbx"},
            {"fonts.lbx", "fntsr.lbx"},
            {"herodata.lbx", "herodatr.lbx"},
            {"help.lbx", "hlpr.lbx"},
            {"hestrngs.lbx", "hrstrngs.lbx"},
            {"jimtext.lbx", "jimtxtr.lbx"},
            {"jimtext2.lbx", "jimtxt2r.lbx"},
            {"kentext.lbx", "kentxtr.lbx"},
            {"kentext1.lbx", "kentxt1r.lbx"},
            {"maintext.lbx", "maintxtr.lbx"},
            {"racename.lbx", "racnamer.lbx"},
            {"racestuf.lbx", "racstufr.lbx"},
            {"rstring0.lbx", "rstrrus0.lbx"},
            {"shipname.lbx", "shipnamr.lbx"},
            {"skildesc.lbx", "skildscr.lbx"},
            {"starname.lbx", "starnamr.lbx"},
            {"techdesc.lbx", "techdesr.lbx"},
            {"techname.lbx", "technamr.lbx"},
        };

        std::string Lower(const char* name) {
            std::string result(name == nullptr ? "" : name);
            for (char& c : result) {
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c == '\\') c = '/';
            }
            return result;
        }

        std::mutex mutex;
        std::set<std::string> logged;   // originals whose twin use was logged
        std::set<std::string> disabled; // originals whose twin is switched off

        bool Map(const char* name, char* out, size_t out_size) {
            const char* twin = Twin_Name(name);
            if (twin == nullptr) return false;
            const std::string key = Lower(name);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (disabled.contains(key)) return false;
            }
            if (!vfs::File_Exists(twin)) return false;
            if (strlen(twin) >= out_size) return false;
            memcpy(out, twin, strlen(twin) + 1);
            std::lock_guard<std::mutex> lock(mutex);
            if (logged.insert(key).second) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "text_language: %s -> %s", key.c_str(), twin);
            }
            return true;
        }

        // CP866 0xB0..0xDF: block elements and box drawing.
        constexpr uint16_t PSEUDOGRAPHICS[48] = {
            0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557,
            0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
            0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
            0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580};
    } // namespace

    const char* Twin_Name(const char* original) {
        if (original == nullptr) return nullptr;
        const std::string key = Lower(original);
        for (const s_twin& entry : TWINS) {
            if (key == entry.original) return entry.twin;
        }
        return nullptr;
    }

    const char* Original_Name(const char* twin) {
        if (twin == nullptr) return nullptr;
        const std::string key = Lower(twin);
        for (const s_twin& entry : TWINS) {
            if (key == entry.twin) return entry.original;
        }
        return nullptr;
    }

    void Install(uint8_t language_value) {
        vfs::Set_Lbx_Name_Mapper(language_value == LANGUAGE_RU ? Map : nullptr);
    }

    void Disable_Twin(const char* original) {
        if (Twin_Name(original) == nullptr) return;
        const std::string key = Lower(original);
        std::lock_guard<std::mutex> lock(mutex);
        if (disabled.insert(key).second) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text_language: %s -> %s is unusable, the original is used", key.c_str(), Twin_Name(original));
        }
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mutex);
        logged.clear();
        disabled.clear();
    }

    bool Cp866_From_Codepoint(uint32_t codepoint, uint8_t* byte) {
        if (codepoint < 0x80) {
            *byte = (uint8_t)codepoint;
            return true;
        }
        if (codepoint >= 0x0410 && codepoint <= 0x042F) {
            *byte = (uint8_t)(0x80 + (codepoint - 0x0410));
            return true;
        }
        if (codepoint >= 0x0430 && codepoint <= 0x043F) {
            *byte = (uint8_t)(0xA0 + (codepoint - 0x0430));
            return true;
        }
        if (codepoint >= 0x0440 && codepoint <= 0x044F) {
            *byte = (uint8_t)(0xE0 + (codepoint - 0x0440));
            return true;
        }
        if (codepoint == 0x0401) {
            *byte = 0xF0;
            return true;
        }
        if (codepoint == 0x0451) {
            *byte = 0xF1;
            return true;
        }
        for (size_t i = 0; i < 48; ++i) {
            if (PSEUDOGRAPHICS[i] == codepoint) {
                *byte = (uint8_t)(0xB0 + i);
                return true;
            }
        }
        return false;
    }

    std::string To_Utf8(std::string_view bytes) {
        // The code point of each CP866 byte, built once from Cp866_From_Codepoint.
        static const std::array<uint16_t, 256> table = [] {
            std::array<uint16_t, 256> t{};
            for (uint32_t i = 0; i < 128; ++i) t[i] = static_cast<uint16_t>(i);
            for (uint32_t codepoint = 0x80; codepoint < 0x2600; ++codepoint) {
                uint8_t byte = 0;
                if (Cp866_From_Codepoint(codepoint, &byte) && byte >= 0x80) t[byte] = static_cast<uint16_t>(codepoint);
            }
            return t;
        }();
        std::string out;
        out.reserve(bytes.size());
        for (const char c : bytes) {
            const uint8_t byte = static_cast<uint8_t>(c);
            if (byte < 0x80 || language != LANGUAGE_RU) {
                out += byte < 0x80 ? c : '?';
                continue;
            }
            const uint32_t codepoint = table[byte];
            if (codepoint == 0) {
                out += '?';
            } else if (codepoint < 0x800) {
                out += static_cast<char>(0xC0 | (codepoint >> 6));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                out += static_cast<char>(0xE0 | (codepoint >> 12));
                out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        }
        return out;
    }

    size_t Decode_Utf8(std::string_view text, size_t index, uint32_t* codepoint) {
        if (index >= text.size()) return 0;
        const auto lead = (unsigned char)text[index];
        size_t length = 0;
        uint32_t value = 0;
        if (lead < 0x80) {
            *codepoint = lead;
            return 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            value = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            value = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            value = lead & 0x07;
        } else {
            return 0;
        }
        if (index + length > text.size()) return 0;
        for (size_t i = 1; i < length; ++i) {
            const auto next = (unsigned char)text[index + i];
            if ((next & 0xC0) != 0x80) return 0;
            value = (value << 6) | (next & 0x3F);
        }
        *codepoint = value;
        return length;
    }

    bool From_Utf8(std::string_view utf8, std::string* bytes) {
        bytes->clear();
        bytes->reserve(utf8.size());
        bool exact = true;
        for (size_t i = 0; i < utf8.size();) {
            uint32_t codepoint = 0;
            uint8_t byte = 0;
            const size_t length = Decode_Utf8(utf8, i, &codepoint);
            if (length == 0) {
                bytes->push_back('?');
                exact = false;
                ++i;
                continue;
            }
            if (Cp866_From_Codepoint(codepoint, &byte)) {
                bytes->push_back((char)byte);
            } else {
                bytes->push_back('?');
                exact = false;
            }
            i += length;
        }
        return exact;
    }
} // namespace text_language
