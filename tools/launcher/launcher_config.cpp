#include "launcher_config.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace launcher {

    const char* const USER_CFG_MARKER = "# orion2re-launcher";

    namespace {
        const char* const PRESET_IDS[PRESET_COUNT] = {"classic", "v150", "living_rivals", "custom"};
        const char* const LANGUAGE_IDS[LANGUAGE_COUNT] = {"en", "ru"};

        void Line_(std::string* out, const std::string& line) {
            *out += line;
            *out += '\n';
        }

        const char* Bool_(bool value) {
            return value ? "true" : "false";
        }

        std::string Trim_(const std::string& text) {
            size_t begin = 0;
            size_t end = text.size();
            while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) ++begin;
            while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
            return text.substr(begin, end - begin);
        }

        std::vector<std::string> Lines_(const std::string& text) {
            std::vector<std::string> lines;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) lines.push_back(Trim_(line));
            return lines;
        }

        bool Starts_With_(const std::string& text, const char* prefix, std::string* rest) {
            const std::string p(prefix);
            if (text.compare(0, p.size(), p) != 0) return false;
            if (rest != nullptr) *rest = Trim_(text.substr(p.size()));
            return true;
        }

        int Clamp_(int value, int low, int high) {
            return value < low ? low : value > high ? high : value;
        }

        std::string Mechanics_Text_(const s_mechanics& m) {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "advisor=%d espionage=%d expansion=%d rivals_adapt=%d diplomacy=%d research_fix=%d memory=%d",
                m.advisor ? 1 : 0, m.espionage, m.expansion, m.rivals_adapt ? 1 : 0, m.diplomacy ? 1 : 0,
                m.research_fix ? 1 : 0, m.memory ? 1 : 0);
            return buffer;
        }

        void Read_Mechanics_(const std::string& text, s_mechanics* m) {
            std::istringstream stream(text);
            std::string pair;
            while (stream >> pair) {
                const size_t eq = pair.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = pair.substr(0, eq);
                const int value = std::atoi(pair.c_str() + eq + 1);
                if (key == "advisor") m->advisor = value != 0;
                else if (key == "espionage") m->espionage = Clamp_(value, 0, 2);
                else if (key == "expansion") m->expansion = Clamp_(value, 0, 2);
                else if (key == "rivals_adapt") m->rivals_adapt = value != 0;
                else if (key == "diplomacy") m->diplomacy = value != 0;
                else if (key == "research_fix") m->research_fix = value != 0;
                else if (key == "memory") m->memory = value != 0;
            }
        }

        std::string Join_(const std::vector<std::string>& items) {
            std::string out;
            for (const std::string& item : items) {
                if (!out.empty()) out += ',';
                out += item;
            }
            return out;
        }

        std::vector<std::string> Split_(const std::string& text) {
            std::vector<std::string> items;
            std::string item;
            std::istringstream stream(text);
            while (std::getline(stream, item, ',')) {
                item = Trim_(item);
                if (!item.empty()) items.push_back(item);
            }
            return items;
        }

        // A mod id is an identifier: letters, digits, '-', '_', '.'.
        bool Valid_Mod_Id_(const std::string& id) {
            if (id.empty()) return false;
            for (char c : id) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.';
                if (!ok) return false;
            }
            return true;
        }

        e_preset Preset_From_Id_(const std::string& id, e_preset fallback) {
            for (int i = 0; i < PRESET_COUNT; ++i) {
                if (id == PRESET_IDS[i]) return static_cast<e_preset>(i);
            }
            return fallback;
        }
    } // namespace

    s_mechanics Preset_Mechanics(e_preset preset) {
        s_mechanics m;
        switch (preset) {
            case PRESET_CLASSIC:
            case PRESET_V150:
                m.advisor = false;
                m.espionage = 0;
                m.expansion = 0;
                m.rivals_adapt = false;
                m.diplomacy = false;
                m.research_fix = false;
                m.memory = false;
                break;
            case PRESET_LIVING_RIVALS:
                m.advisor = true;
                m.espionage = 2;  // includes 60 PP spies (mechanics.md 3.2.6, lever 3)
                m.expansion = 1;  // outposts stay off: they cost long games
                m.rivals_adapt = true;
                m.diplomacy = true;
                m.research_fix = true;
                m.memory = true;  // README 10.4.4: wars end sooner, galaxies grow more
                break;
            default:
                break;
        }
        return m;
    }

    std::string Build_User_Cfg(const s_settings& settings) {
        const e_preset preset = (settings.preset >= 0 && settings.preset < PRESET_COUNT) ? settings.preset : PRESET_CUSTOM;
        const s_mechanics m = preset == PRESET_CUSTOM ? settings.mechanics : Preset_Mechanics(preset);
        std::vector<std::string> mods;
        if (preset == PRESET_V150) mods.push_back("v150");
        if (preset == PRESET_CUSTOM) {
            for (const std::string& id : settings.mods) {
                if (Valid_Mod_Id_(id)) mods.push_back(id);
            }
        }

        std::string out;
        Line_(&out, std::string(USER_CFG_MARKER) + " - written by orion2re-launcher; change it there or delete this file");
        Line_(&out, std::string("# preset: ") + PRESET_IDS[preset]);
        Line_(&out, "# mechanics: " + Mechanics_Text_(m));
        Line_(&out, "# mods: " + Join_(mods));
        Line_(&out, "");
        for (const std::string& id : mods) Line_(&out, "enable " + id + ";");

        Line_(&out, std::string("advisor enabled = ") + Bool_(m.advisor) + ";");
        if (m.advisor) {
            Line_(&out, std::string("advisor rules_spies = ") + Bool_(m.espionage > 0) + ";");
            Line_(&out, std::string("advisor rules_expansion = ") + Bool_(m.expansion > 0) + ";");
            Line_(&out, std::string("advisor rules_outposts = ") + Bool_(m.expansion > 1) + ";");
            Line_(&out, std::string("advisor rules_objectives = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_exploration = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_lanchester = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_counter_design = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_balance = ") + Bool_(m.diplomacy) + ";");
            Line_(&out, std::string("advisor rules_doctrine = ") + Bool_(m.diplomacy) + ";");
        }
        if (m.espionage > 1) Line_(&out, "colony misc_costs spy = 60;");
        if (m.research_fix) Line_(&out, "ai research_uses_own_output = true;");
        if (m.memory) Line_(&out, "diplomacy memory = true;");
        return out;
    }

    bool Is_Launcher_User_Cfg(const std::string& text) {
        return text.compare(0, std::string(USER_CFG_MARKER).size(), USER_CFG_MARKER) == 0;
    }

    bool Read_User_Cfg(const std::string& text, s_settings* settings) {
        if (settings == nullptr || !Is_Launcher_User_Cfg(text)) return false;
        s_settings read = *settings;
        bool have_preset = false;
        for (const std::string& line : Lines_(text)) {
            std::string rest;
            if (Starts_With_(line, "# preset:", &rest)) {
                read.preset = Preset_From_Id_(rest, PRESET_CUSTOM);
                have_preset = true;
            } else if (Starts_With_(line, "# mechanics:", &rest)) {
                Read_Mechanics_(rest, &read.mechanics);
            } else if (Starts_With_(line, "# mods:", &rest)) {
                read.mods.clear();
                for (const std::string& id : Split_(rest)) {
                    if (Valid_Mod_Id_(id) && !(read.preset == PRESET_V150 && id == "v150")) read.mods.push_back(id);
                }
            }
        }
        if (!have_preset) return false;
        *settings = read;
        return true;
    }

    e_language Language_For_Game_Dir(const std::string& game_dir) {
        std::string lower = game_dir;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return lower.find("rus") != std::string::npos ? LANGUAGE_RUSSIAN : LANGUAGE_ENGLISH;
    }

    std::string Build_Launcher_Ini(const s_settings& settings) {
        std::string out;
        Line_(&out, std::string("language=") + LANGUAGE_IDS[settings.language == LANGUAGE_RUSSIAN ? 1 : 0]);
        Line_(&out, std::string("preset=") + PRESET_IDS[(settings.preset >= 0 && settings.preset < PRESET_COUNT) ? settings.preset : PRESET_CUSTOM]);
        Line_(&out, "game_dir_en=" + settings.game_dir[LANGUAGE_ENGLISH]);
        Line_(&out, "game_dir_ru=" + settings.game_dir[LANGUAGE_RUSSIAN]);
        Line_(&out, "mechanics=" + Mechanics_Text_(settings.mechanics));
        Line_(&out, "mods=" + Join_(settings.mods));
        return out;
    }

    void Read_Launcher_Ini(const std::string& text, s_settings* settings) {
        if (settings == nullptr) return;
        for (const std::string& line : Lines_(text)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim_(line.substr(0, eq));
            const std::string value = Trim_(line.substr(eq + 1));
            if (key == "language") settings->language = value == "ru" ? LANGUAGE_RUSSIAN : LANGUAGE_ENGLISH;
            else if (key == "preset") settings->preset = Preset_From_Id_(value, settings->preset);
            else if (key == "game_dir_en") settings->game_dir[LANGUAGE_ENGLISH] = value;
            else if (key == "game_dir_ru") settings->game_dir[LANGUAGE_RUSSIAN] = value;
            else if (key == "mechanics") Read_Mechanics_(value, &settings->mechanics);
            else if (key == "mods") {
                settings->mods.clear();
                for (const std::string& id : Split_(value)) {
                    if (Valid_Mod_Id_(id)) settings->mods.push_back(id);
                }
            }
        }
    }

    std::vector<std::string> Game_Arguments(const std::string& game_exe, const s_settings& settings) {
        std::vector<std::string> args{game_exe};
        const std::string& dir = settings.game_dir[settings.language == LANGUAGE_RUSSIAN ? 1 : 0];
        if (!dir.empty()) {
#if defined(_WIN32)
            args.push_back("/game-dir");
#else
            args.push_back("-game-dir");
#endif
            args.push_back(dir);
        }
        return args;
    }

} // namespace launcher
