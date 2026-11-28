#pragma once

// Launcher logic without any UI (docs/LocalLLM/launcher.md): presets and sliders to `user.cfg`
// statements, reading a launcher-written `user.cfg` back, the launcher's own settings file, and the
// game's command line. The game reads `<exe dir>/data/user.cfg` after its main config, so the
// launcher never touches `orion2re.cfg`.

#include <string>
#include <vector>

namespace launcher {

    enum e_preset : int {
        PRESET_CLASSIC = 0,        // the original game, advisor off
        PRESET_V150 = 1,           // the 1.50 mod, advisor off
        PRESET_LIVING_RIVALS = 2,  // advisor on with every measured mechanic
        PRESET_ALL_NEW = 3,        // Living rivals plus every box of mechanics.json5 (the playtest setup)
        PRESET_CUSTOM = 4,         // whatever the sliders and boxes say
        PRESET_COUNT = 5,
    };

    enum e_language : int {
        LANGUAGE_ENGLISH = 0,
        LANGUAGE_RUSSIAN = 1,
        LANGUAGE_COUNT = 2,
    };

    // Slider positions: 0 = off, 1 = normal, 2 = strong.
    struct s_mechanics {
        bool advisor = true;
        int espionage = 1;         // 1: rules_spies; 2: also 60 PP spies and a doubled spy-target weight
        int expansion = 1;         // 1: colony ships; 2: also outposts (cost long games, see README 10.4.2)
        bool rivals_adapt = true;  // objectives, exploration, Lanchester and decisive force
        bool diplomacy = true;     // coalition against the leader and the Byzantine doctrine
        bool phases = true;        // advisor rules_phases: leaders conquer, everyone resists a would-be winner
        bool research_fix = true;  // ai research_uses_own_output
        bool memory = true;        // diplomacy memory: trust heals, grievances fade, war weariness (every empire)
    };

    struct s_settings {
        e_preset preset = PRESET_LIVING_RIVALS;
        e_language language = LANGUAGE_ENGLISH;
        std::string game_dir[LANGUAGE_COUNT];  // original data folder per language (v1_31eng, v1_31rus)
        s_mechanics mechanics;                 // used by PRESET_CUSTOM
        std::vector<std::string> mods;         // extra `enable <id>;` for PRESET_CUSTOM
        std::vector<std::string> new_mechanics;  // checked boxes of the mechanics page, "namespace.name"
    };

    // One setting of the settings catalog (settings_catalog.json, written by `config-tool catalog`).
    struct s_catalog_entry {
        std::string name_space, name, group, category, title, description, remarks, type;
        std::string default_text;  // "true", "false", a number, or an enum symbol
        bool default_bool = false;
    };

    struct s_catalog {
        std::vector<s_catalog_entry> entries;
        const s_catalog_entry* Find(const std::string& name_space, const std::string& name) const;
    };

    // The "new mechanics" list (mechanics.json5): bool settings grouped by documentation category.
    struct s_mechanic_ref {
        std::string name_space, name;
    };
    struct s_mechanic_category {
        std::string id, title;
        std::vector<s_mechanic_ref> settings;
    };
    struct s_mechanics_list {
        std::vector<s_mechanic_category> categories;
        size_t Count() const;
    };

    // False (and the output cleared) when the text is not the expected JSON / JSON5.
    bool Parse_Catalog(const std::string& json, s_catalog* catalog);
    bool Parse_Mechanics_List(const std::string& json5, s_mechanics_list* list);

    // The boxes of the mechanics page: keys are "namespace.name".
    bool Mechanic_On(const s_settings& settings, const std::string& name_space, const std::string& name);
    void Set_Mechanic(s_settings* settings, const std::string& name_space, const std::string& name, bool on);

    // First line of every launcher-written user.cfg; a file without it was written by hand.
    extern const char* const USER_CFG_MARKER;

    // The mechanics a preset stands for (PRESET_CUSTOM: the defaults).
    s_mechanics Preset_Mechanics(e_preset preset);

    // The complete user.cfg text for `settings` (marker line, comments, statements). `list` is the
    // mechanics list: PRESET_ALL_NEW turns every box of it on; for PRESET_CUSTOM only boxes in it are
    // written. Without a list (nullptr) PRESET_ALL_NEW is Living rivals and custom boxes are written as is.
    // The text language is written as `interface language = ru;` (nothing for English).
    std::string Build_User_Cfg(const s_settings& settings, const s_mechanics_list* list = nullptr);

    // True when `text` was written by the launcher (starts with USER_CFG_MARKER).
    bool Is_Launcher_User_Cfg(const std::string& text);

    // Reads the preset, mechanics and mods back from a launcher-written user.cfg. False (and
    // `settings` unchanged) when the text does not parse or was not written by the launcher.
    bool Read_User_Cfg(const std::string& text, s_settings* settings);

    // The language whose texts a data folder holds: Russian when its name contains "rus".
    e_language Language_For_Game_Dir(const std::string& game_dir);

    // The launcher's own settings file (launcher.ini next to the executable), key=value lines.
    std::string Build_Launcher_Ini(const s_settings& settings);
    void Read_Launcher_Ini(const std::string& text, s_settings* settings);

    // Arguments to start the game: executable path, then /game-dir <folder of the chosen language>
    // when that folder is set.
    std::vector<std::string> Game_Arguments(const std::string& game_exe, const s_settings& settings);

} // namespace launcher
