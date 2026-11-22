#pragma once

#include <cstdint>
#include <string>

#include "command_line.h"

namespace startup {
    struct s_startup_options {
        std::string config_path = "orion2re.cfg";
        bool config_selected = false;
        bool skip_intro = false;
        int32_t load_save = 0; // SAVE<n>.GAM, 1-10; zero means no request.
        std::string game_directory;
        std::string oracle_record;
        std::string oracle_replay;
        int32_t oracle_turns = 100;
        uint32_t oracle_seed = 1;        // game seed for oracle-record (self-play bench); never zero
        int32_t oracle_difficulty = -1;  // 0..4 for oracle-record; -1 keeps the configured difficulty
        bool oracle_tactical = false;    // oracle-record with tactical-combat settings (AI ship designs for
                                         // tactical combat; battles between computer players stay strategic)
        int32_t oracle_players = -1;     // 2..8 empires for oracle-record; -1 keeps the configured number
    };

    bool Parse_Startup_Options(int argc, const char* const* argv, s_startup_options* options, std::string* error, char prefix = command_line::OPTION_PREFIX);
} // namespace startup
