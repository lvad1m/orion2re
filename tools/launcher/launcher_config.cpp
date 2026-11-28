#include "launcher_config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>

namespace launcher {

    const char* const USER_CFG_MARKER = "# orion2re-launcher";

    namespace {
        const char* const PRESET_IDS[PRESET_COUNT] = {"classic", "v150", "living_rivals", "all_new", "custom"};
        // Boxes the preset "All new mechanics" leaves off: tools of the computer-only benches and logs, not
        // mechanics of a game with a human (the author's playtest configuration of 27.11.2026 leaves out the
        // all-computer council on purpose). They stay on the mechanics page for a custom setup.
        const char* const ALL_NEW_SKIP[] = {"gameplay.council_without_humans", "dice.combat_log"};

        bool All_New_Skips_(const std::string& name_space, const std::string& name) {
            const std::string key = name_space + "." + name;
            for (const char* skip : ALL_NEW_SKIP) {
                if (key == skip) return true;
            }
            return false;
        }
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
            std::snprintf(buffer, sizeof(buffer), "advisor=%d espionage=%d expansion=%d rivals_adapt=%d diplomacy=%d phases=%d research_fix=%d memory=%d",
                m.advisor ? 1 : 0, m.espionage, m.expansion, m.rivals_adapt ? 1 : 0, m.diplomacy ? 1 : 0,
                m.phases ? 1 : 0, m.research_fix ? 1 : 0, m.memory ? 1 : 0);
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
                else if (key == "phases") m->phases = value != 0;
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

        // A small JSON reader for the catalog and the mechanics list: objects, arrays, strings, numbers,
        // true/false/null; also `//` comments, unquoted keys and trailing commas (JSON5 as written by
        // make_mechanics_list.py).
        struct s_json {
            enum e_kind { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT } kind = NUL;
            bool boolean = false;
            std::string text;  // the string, or the number as written
            std::vector<s_json> items;
            std::vector<std::pair<std::string, s_json>> members;
            const s_json* Get(const char* key) const {
                for (const auto& member : members) {
                    if (member.first == key) return &member.second;
                }
                return nullptr;
            }
            std::string Str(const char* key) const {
                const s_json* v = Get(key);
                return v != nullptr && v->kind == STRING ? v->text : std::string();
            }
        };

        class Json_Reader_ {
        public:
            explicit Json_Reader_(const std::string& text) : text_(text) {}
            bool Parse(s_json* out) {
                if (!Value_(out, 0)) return false;
                Skip_();
                return pos_ == text_.size();
            }

        private:
            const std::string& text_;
            size_t pos_ = 0;

            void Skip_() {
                while (pos_ < text_.size()) {
                    const char c = text_[pos_];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                        ++pos_;
                    } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                    } else {
                        break;
                    }
                }
            }

            bool Literal_(const char* word) {
                const size_t n = std::strlen(word);
                if (text_.compare(pos_, n, word) != 0) return false;
                pos_ += n;
                return true;
            }

            static void Append_Utf8_(std::string* out, unsigned code) {
                if (code < 0x80) {
                    *out += static_cast<char>(code);
                } else if (code < 0x800) {
                    *out += static_cast<char>(0xC0 | (code >> 6));
                    *out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    *out += static_cast<char>(0xE0 | (code >> 12));
                    *out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    *out += static_cast<char>(0x80 | (code & 0x3F));
                }
            }

            bool String_(std::string* out) {
                if (pos_ >= text_.size() || text_[pos_] != '"') return false;
                ++pos_;
                out->clear();
                while (pos_ < text_.size()) {
                    const char c = text_[pos_++];
                    if (c == '"') return true;
                    if (c != '\\') {
                        *out += c;
                        continue;
                    }
                    if (pos_ >= text_.size()) return false;
                    const char e = text_[pos_++];
                    switch (e) {
                        case 'n': *out += '\n'; break;
                        case 't': *out += '\t'; break;
                        case 'r': *out += '\r'; break;
                        case 'b': *out += '\b'; break;
                        case 'f': *out += '\f'; break;
                        case 'u': {
                            if (pos_ + 4 > text_.size()) return false;
                            unsigned code = 0;
                            for (int i = 0; i < 4; ++i) {
                                const char h = text_[pos_++];
                                code <<= 4;
                                if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                                else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                                else return false;
                            }
                            Append_Utf8_(out, code);
                            break;
                        }
                        default: *out += e; break;  // \" \\ \/
                    }
                }
                return false;
            }

            bool Key_(std::string* out) {
                if (pos_ < text_.size() && text_[pos_] == '"') return String_(out);
                const size_t begin = pos_;
                while (pos_ < text_.size() &&
                    ((text_[pos_] >= 'a' && text_[pos_] <= 'z') || (text_[pos_] >= 'A' && text_[pos_] <= 'Z') ||
                        (text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '_')) {
                    ++pos_;
                }
                if (pos_ == begin) return false;
                *out = text_.substr(begin, pos_ - begin);
                return true;
            }

            bool Value_(s_json* out, int depth) {
                if (depth > 16) return false;
                Skip_();
                if (pos_ >= text_.size()) return false;
                const char c = text_[pos_];
                if (c == '{') {
                    ++pos_;
                    out->kind = s_json::OBJECT;
                    for (;;) {
                        Skip_();
                        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
                        std::pair<std::string, s_json> member;
                        if (!Key_(&member.first)) return false;
                        Skip_();
                        if (pos_ >= text_.size() || text_[pos_] != ':') return false;
                        ++pos_;
                        if (!Value_(&member.second, depth + 1)) return false;
                        out->members.push_back(std::move(member));
                        Skip_();
                        if (pos_ < text_.size() && text_[pos_] == ',') ++pos_;
                        else if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
                        else return false;
                    }
                }
                if (c == '[') {
                    ++pos_;
                    out->kind = s_json::ARRAY;
                    for (;;) {
                        Skip_();
                        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
                        s_json item;
                        if (!Value_(&item, depth + 1)) return false;
                        out->items.push_back(std::move(item));
                        Skip_();
                        if (pos_ < text_.size() && text_[pos_] == ',') ++pos_;
                        else if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
                        else return false;
                    }
                }
                if (c == '"') {
                    out->kind = s_json::STRING;
                    return String_(&out->text);
                }
                if (Literal_("true")) { out->kind = s_json::BOOL; out->boolean = true; return true; }
                if (Literal_("false")) { out->kind = s_json::BOOL; out->boolean = false; return true; }
                if (Literal_("null")) { out->kind = s_json::NUL; return true; }
                const size_t begin = pos_;
                while (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+' || text_[pos_] == '.' ||
                    text_[pos_] == 'e' || text_[pos_] == 'E' || (text_[pos_] >= '0' && text_[pos_] <= '9'))) {
                    ++pos_;
                }
                if (pos_ == begin) return false;
                out->kind = s_json::NUMBER;
                out->text = text_.substr(begin, pos_ - begin);
                return true;
            }
        };

        // A setting key part: lower-case letters, digits, '_' (what config names use).
        bool Valid_Key_Part_(const std::string& part) {
            if (part.empty()) return false;
            for (char c : part) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
            }
            return true;
        }

        // "namespace.name" -> its two parts; false when malformed.
        bool Split_Key_(const std::string& key, std::string* name_space, std::string* name) {
            const size_t dot = key.find('.');
            if (dot == std::string::npos) return false;
            *name_space = key.substr(0, dot);
            *name = key.substr(dot + 1);
            return Valid_Key_Part_(*name_space) && Valid_Key_Part_(*name);
        }

        bool In_List_(const s_mechanics_list& list, const std::string& name_space, const std::string& name) {
            for (const s_mechanic_category& category : list.categories) {
                for (const s_mechanic_ref& ref : category.settings) {
                    if (ref.name_space == name_space && ref.name == name) return true;
                }
            }
            return false;
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
                m.phases = false;
                m.research_fix = false;
                m.memory = false;
                break;
            case PRESET_LIVING_RIVALS:
            case PRESET_ALL_NEW:
                m.advisor = true;
                m.espionage = 2;  // includes 60 PP spies (mechanics.md 3.2.6, lever 3)
                m.expansion = 1;  // outposts stay off: they cost long games
                m.rivals_adapt = true;
                m.diplomacy = true;
                m.phases = true;  // README 10.7: a leader plays for victory, coalitions resist it (decided 21.11.2026)
                m.research_fix = true;
                m.memory = true;  // README 10.4.4: wars end sooner, galaxies grow more
                break;
            default:
                break;
        }
        return m;
    }

    const s_catalog_entry* s_catalog::Find(const std::string& name_space, const std::string& name) const {
        for (const s_catalog_entry& entry : entries) {
            if (entry.name_space == name_space && entry.name == name) return &entry;
        }
        return nullptr;
    }

    size_t s_mechanics_list::Count() const {
        size_t count = 0;
        for (const s_mechanic_category& category : categories) count += category.settings.size();
        return count;
    }

    bool Parse_Catalog(const std::string& json, s_catalog* catalog) {
        if (catalog == nullptr) return false;
        catalog->entries.clear();
        s_json root;
        if (!Json_Reader_(json).Parse(&root) || root.kind != s_json::ARRAY) return false;
        for (const s_json& item : root.items) {
            if (item.kind != s_json::OBJECT) { catalog->entries.clear(); return false; }
            s_catalog_entry entry;
            entry.name_space = item.Str("namespace");
            entry.name = item.Str("name");
            entry.group = item.Str("group");
            entry.category = item.Str("category");
            entry.title = item.Str("title");
            entry.description = item.Str("description");
            entry.remarks = item.Str("remarks");
            entry.type = item.Str("type");
            if (entry.name.empty()) { catalog->entries.clear(); return false; }
            const s_json* def = item.Get("default");
            if (def != nullptr) {
                if (def->kind == s_json::BOOL) {
                    entry.default_bool = def->boolean;
                    entry.default_text = def->boolean ? "true" : "false";
                } else if (def->kind == s_json::STRING || def->kind == s_json::NUMBER) {
                    entry.default_text = def->text;
                }
            }
            catalog->entries.push_back(std::move(entry));
        }
        return true;
    }

    bool Parse_Mechanics_List(const std::string& json5, s_mechanics_list* list) {
        if (list == nullptr) return false;
        list->categories.clear();
        s_json root;
        if (!Json_Reader_(json5).Parse(&root) || root.kind != s_json::OBJECT) return false;
        const s_json* categories = root.Get("categories");
        if (categories == nullptr || categories->kind != s_json::ARRAY) return false;
        for (const s_json& item : categories->items) {
            s_mechanic_category category;
            category.id = item.Str("id");
            category.title = item.Str("title");
            const s_json* settings = item.Get("settings");
            if (settings == nullptr || settings->kind != s_json::ARRAY) { list->categories.clear(); return false; }
            for (const s_json& setting : settings->items) {
                s_mechanic_ref ref{setting.Str("namespace"), setting.Str("name")};
                if (!Valid_Key_Part_(ref.name_space) || !Valid_Key_Part_(ref.name)) { list->categories.clear(); return false; }
                category.settings.push_back(std::move(ref));
            }
            list->categories.push_back(std::move(category));
        }
        return true;
    }

    bool Mechanic_On(const s_settings& settings, const std::string& name_space, const std::string& name) {
        const std::string key = name_space + "." + name;
        return std::find(settings.new_mechanics.begin(), settings.new_mechanics.end(), key) != settings.new_mechanics.end();
    }

    void Set_Mechanic(s_settings* settings, const std::string& name_space, const std::string& name, bool on) {
        const std::string key = name_space + "." + name;
        auto it = std::find(settings->new_mechanics.begin(), settings->new_mechanics.end(), key);
        if (on && it == settings->new_mechanics.end()) settings->new_mechanics.push_back(key);
        else if (!on && it != settings->new_mechanics.end()) settings->new_mechanics.erase(it);
    }

    std::string Build_User_Cfg(const s_settings& settings, const s_mechanics_list* list) {
        const e_preset preset = (settings.preset >= 0 && settings.preset < PRESET_COUNT) ? settings.preset : PRESET_CUSTOM;
        const s_mechanics m = preset == PRESET_CUSTOM ? settings.mechanics : Preset_Mechanics(preset);
        // The boxes written as `<namespace> <name> = true;`, as (namespace, name) pairs.
        std::vector<std::pair<std::string, std::string>> boxes;
        if (preset == PRESET_ALL_NEW && list != nullptr) {
            for (const s_mechanic_category& category : list->categories) {
                for (const s_mechanic_ref& ref : category.settings) {
                    if (!All_New_Skips_(ref.name_space, ref.name)) boxes.emplace_back(ref.name_space, ref.name);
                }
            }
        } else if (preset == PRESET_CUSTOM) {
            for (const std::string& key : settings.new_mechanics) {
                std::string name_space, name;
                if (!Split_Key_(key, &name_space, &name)) continue;
                if (list != nullptr && !In_List_(*list, name_space, name)) continue;
                boxes.emplace_back(name_space, name);
            }
        }
        std::string box_keys;
        for (const auto& box : boxes) {
            if (!box_keys.empty()) box_keys += ',';
            box_keys += box.first + "." + box.second;
        }
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
        Line_(&out, std::string("# language: ") + LANGUAGE_IDS[settings.language == LANGUAGE_RUSSIAN ? 1 : 0]);
        Line_(&out, "# new_mechanics: " + box_keys);
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
            Line_(&out, std::string("advisor rules_seen_ships = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_race = ") + Bool_(m.rivals_adapt) + ";");
            Line_(&out, std::string("advisor rules_balance = ") + Bool_(m.diplomacy) + ";");
            Line_(&out, std::string("advisor rules_doctrine = ") + Bool_(m.diplomacy) + ";");
            Line_(&out, std::string("advisor rules_phases = ") + Bool_(m.phases) + ";");
        }
        if (m.espionage > 1) Line_(&out, "colony misc_costs spy = 60;");
        if (m.research_fix) Line_(&out, "ai research_uses_own_output = true;");
        if (m.memory) Line_(&out, "diplomacy memory = true;");
        for (const auto& box : boxes) Line_(&out, box.first + " " + box.second + " = true;");
        if (preset == PRESET_ALL_NEW && list != nullptr) {
            // The numbers of the author's playtest configuration (27.11.2026, the benches' "all"): every spy
            // operation, the spy network's growth by the target's size, the weariness that stops a computer's
            // new wars, the advisor's full journal (the chronicle reads it). The mechanics page has no number
            // boxes yet, so the preset writes them itself.
            Line_(&out, "spy operations = 16383;");
            Line_(&out, "spy network_size_step = 8;");
            Line_(&out, "diplomacy memory_weary_limit = 30;");
            Line_(&out, "advisor journal = full;");
        }
        if (settings.language == LANGUAGE_RUSSIAN) Line_(&out, "interface language = ru;");
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
            } else if (Starts_With_(line, "# language:", &rest)) {
                read.language = rest == "ru" ? LANGUAGE_RUSSIAN : LANGUAGE_ENGLISH;
            } else if (Starts_With_(line, "# new_mechanics:", &rest)) {
                read.new_mechanics.clear();
                for (const std::string& key : Split_(rest)) {
                    std::string name_space, name;
                    if (Split_Key_(key, &name_space, &name)) Set_Mechanic(&read, name_space, name, true);
                }
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
        Line_(&out, "new_mechanics=" + Join_(settings.new_mechanics));
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
            else if (key == "new_mechanics") {
                settings->new_mechanics.clear();
                for (const std::string& item : Split_(value)) {
                    std::string name_space, name;
                    if (Split_Key_(item, &name_space, &name)) Set_Mechanic(settings, name_space, name, true);
                }
            } else if (key == "mods") {
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
