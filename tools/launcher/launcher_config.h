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
        PRESET_CUSTOM = 3,         // whatever the sliders say
        PRESET_COUNT = 4,
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
    };

    // First line of every launcher-written user.cfg; a file without it was written by hand.
    extern const char* const USER_CFG_MARKER;

    // The mechanics a preset stands for (PRESET_CUSTOM: the defaults).
    s_mechanics Preset_Mechanics(e_preset preset);

    // The complete user.cfg text for `settings` (marker line, comments, statements).
    std::string Build_User_Cfg(const s_settings& settings);

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
