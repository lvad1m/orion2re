#include "pch.h"

#include "game/advisor_journal.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "game/advisor.h"
#include "game/user_paths.h"

namespace advisor {

    namespace {
        const char* const source_names[] = {"none", "rules", "llm", "debug"};
        const char* const posture_names[] = {
            "keep", "make_peace", "defend", "attack_current_enemies", "start_new_war"};
        const char* const attitude_names[] = {
            "keep", "ally", "friendly", "neutral", "wary", "hostile", "war"};
        const char* const level_names[] = {"keep", "low", "normal", "high", "max"};
        const char* const research_names[] = {"keep",
            "balanced",
            "weapons",
            "shields_armor",
            "computers_ecm",
            "propulsion",
            "planetology",
            "economy",
            "biology"};
        const char* const doctrine_names[] = {
            "keep", "balanced", "anti_missile", "anti_beam", "anti_fighter", "anti_boarding"};
        const char* const spy_mission_names[] = {
            "keep", "none", "gather_intel", "steal_tech", "sabotage", "counterintel"};
        const char* const weight_keys[WEIGHT_COUNT] = {
            "fleet", "defense", "colonization", "economy", "food", "research"};

        struct s_journal_state {
            std::string directory_override;
            std::ofstream file;
            bool open = false;
            bool disabled = false;
        };

        s_journal_state state;

        template <size_t N>
        const char* Name_Of(const char* const (&names)[N], uint8_t value) {
            return value < N ? names[value] : "invalid";
        }

        void Append_Quoted(const std::string& text, std::string* out) {
            out->push_back('"');
            Append_Json_Escaped(text, out);
            out->push_back('"');
        }

        void Append_Int(int64_t value, std::string* out) {
            out->append(std::to_string(value));
        }

        // Player numbers and objectives: 0xFF means "none" and becomes null.
        void Append_Player(uint8_t value, std::string* out) {
            if (value == NO_PLAYER)
                out->append("null");
            else
                Append_Int(value, out);
        }

        void Append_Key(const char* key, std::string* out) {
            Append_Quoted(key, out);
            out->push_back(':');
        }

        void Append_Directive(const s_directive* d, std::string* out) {
            out->append("{");
            Append_Key("objective", out);
            Append_Player(d->objective, out);
            out->append(",");
            Append_Key("posture", out);
            Append_Quoted(Name_Of(posture_names, d->posture), out);
            out->append(",");
            Append_Key("attitude", out);
            out->push_back('[');
            for (int32_t i = 0; i < MAX_PLAYERS; i++) {
                if (i > 0) out->push_back(',');
                Append_Quoted(Name_Of(attitude_names, d->attitude[i]), out);
            }
            out->append("],");
            Append_Key("main_rival", out);
            Append_Player(d->main_rival, out);
            out->append(",");
            Append_Key("weights", out);
            out->push_back('{');
            for (int32_t i = 0; i < WEIGHT_COUNT; i++) {
                if (i > 0) out->push_back(',');
                Append_Key(weight_keys[i], out);
                Append_Quoted(Name_Of(level_names, d->weights[i]), out);
            }
            out->append("},");
            Append_Key("research_focus", out);
            Append_Quoted(Name_Of(research_names, d->research_focus), out);
            out->append(",");
            Append_Key("fleet_doctrine", out);
            Append_Quoted(Name_Of(doctrine_names, d->fleet_doctrine), out);
            out->append(",");
            Append_Key("spy_target", out);
            Append_Player(d->spy_target, out);
            out->append(",");
            Append_Key("spy_mission", out);
            Append_Quoted(Name_Of(spy_mission_names, d->spy_mission), out);
            out->append(",");
            Append_Key("issued", out);
            Append_Int(d->issued_stardate, out);
            out->append(",");
            Append_Key("expires", out);
            Append_Int(d->expires_stardate, out);
            out->append(",");
            Append_Key("rule_id", out);
            Append_Int(d->rule_id, out);
            out->push_back('}');
        }

        void Warn_And_Disable(const char* what) {
            if (!state.disabled)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Advisor journal disabled: %s", what);
            state.disabled = true;
            if (state.file.is_open()) state.file.close();
            state.open = false;
        }

        std::string Session_Name() {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local = {};
#if defined(_WIN32)
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
            return buffer;
        }

        bool Open_Session_File() {
            std::string directory = state.directory_override;
            if (directory.empty()) {
                const char* base = user_paths::Get_Directory(user_paths::USER_DIRECTORY_STATE);
                if (base == nullptr || base[0] == '\0') {
                    Warn_And_Disable("user paths are disabled");
                    return false;
                }
                directory = std::string(base) + "advisor_journal/";
            }
            std::error_code error;
            const std::filesystem::path dir_path(directory);
            std::filesystem::create_directories(dir_path, error);
            if (error) {
                Warn_And_Disable("cannot create the journal directory");
                return false;
            }
            const std::string session = Session_Name();
            std::filesystem::path file_path = dir_path / (session + ".jsonl");
            for (int32_t counter = 2; std::filesystem::exists(file_path, error) && counter < 10000; counter++)
                file_path = dir_path / (session + "-" + std::to_string(counter) + ".jsonl");
            state.file.clear();
            state.file.open(file_path, std::ios::out | std::ios::app | std::ios::binary);
            if (!state.file.is_open()) {
                Warn_And_Disable("cannot open the journal file");
                return false;
            }
            state.open = true;
            return true;
        }
    } // namespace

    void Append_Json_Escaped(const std::string& text, std::string* out) {
        static const char hex[] = "0123456789abcdef";
        for (char c : text) {
            const auto byte = static_cast<uint8_t>(c);
            switch (c) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            default:
                if (byte < 0x20) {
                    out->append("\\u00");
                    out->push_back(hex[byte >> 4]);
                    out->push_back(hex[byte & 15]);
                } else {
                    out->push_back(c);
                }
            }
        }
    }

    void Format_Journal_Line(const s_journal_entry* entry, uint8_t mode, std::string* out) {
        out->clear();
        if (entry == nullptr) return;
        const s_directive* d = entry->directive;
        out->append("{");
        Append_Key("stardate", out);
        Append_Int(entry->stardate, out);
        out->append(",");
        Append_Key("player", out);
        Append_Int(entry->player, out);
        out->append(",");
        Append_Key("source", out);
        Append_Quoted(d != nullptr ? Name_Of(source_names, d->source) : "none", out);
        out->append(",");
        Append_Key("accepted", out);
        out->append(entry->accepted ? "true" : "false");
        out->append(",");
        Append_Key("rejection", out);
        Append_Quoted(entry->rejection, out);
        out->append(",");
        Append_Key("rule_note", out);
        Append_Quoted(entry->rule_note, out);
        out->append(",");
        Append_Key("directive", out);
        if (d != nullptr)
            Append_Directive(d, out);
        else
            out->append("null");
        if (mode == JOURNAL_FULL && entry->view != nullptr) {
            std::string summary;
            Format_Empire_Summary(entry->view, &summary);
            out->append(",");
            Append_Key("summary", out);
            Append_Quoted(summary, out);
        }
        out->push_back('}');
    }

    void Write_Journal_Line(const std::string& line) {
        if (state.disabled) return;
        if (!state.open && !Open_Session_File()) return;
        state.file.write(line.data(), static_cast<std::streamsize>(line.size()));
        state.file.put('\n');
        state.file.flush();
        if (!state.file) Warn_And_Disable("write failed");
    }

    void Close_Journal() {
        if (state.file.is_open()) state.file.close();
        state.file.clear();
        state.open = false;
        state.disabled = false;
    }

    void Set_Journal_Directory(const std::string& directory) {
        state.directory_override = directory;
    }

} // namespace advisor
