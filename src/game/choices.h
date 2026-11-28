#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Choices of a human in events (docs/LocalLLM/human-ui.md section 5, slice R1). An event module that would
// pick an option for a human (the classic rule: the first, safest one) may instead pick its subject as
// always and queue a pending choice here. At the start of the human's turn, in the report chain
// (report.cpp Display_Report_Aux, after the diplomacy messages), a window shows what happened and the
// options; the picked option is applied at once. A subject that is gone by then drops the choice silently.
// Computers never use this. Off by default (events human_choices); with the setting off nothing is queued,
// nothing is kept and the save section is not written. Nothing here draws from game_random.
namespace choices {

    extern bool enabled;  // events human_choices

    constexpr int32_t MAX_PENDING = 16;

    // The modules that queue choices (a later piece adds its own value here and its case in choices.cpp).
    enum e_module : uint8_t {
        MODULE_NONE = 0,
        MODULE_INCIDENTS = 1,
        MODULE_LAST = 1,
    };

    // One pending choice. What target, other and value mean is the module's business (incidents: the colony,
    // the leader or the other empire, the sum).
    struct s_choice {
        uint8_t module = MODULE_NONE;
        uint8_t kind = 0;
        int8_t player = -1;
        int16_t target = -1;
        int16_t other = -1;
        int32_t value = 0;
        int32_t asked = 0;  // the stardate it was queued
    };

    // What the window shows (English, plain ASCII).
    struct s_text {
        char title[64] = {};
        char body[512] = {};
        char option[3][64] = {};
        int32_t count = 0;  // the options, 2 or 3
    };

    struct s_state {
        int32_t count = 0;
        s_choice choice[MAX_PENDING];
    };
    s_state* State();

    // The setting is on and `player` exists, is alive and human, and the game is not a network game.
    bool Ask_Human(int16_t player);

    // Queues a choice; false when the queue is full.
    bool Add(const s_choice& choice);
    int32_t Count(int16_t player);

    // Applies the first pending choice of `player` with `option` and removes it; false when it has none.
    bool Apply_Pending(int16_t player, int32_t option);

    // A step of the report chain (same convention as Has_Officer_For_Hire: non-zero while there is
    // something): drops (with a log line) the player's choices whose subject is gone; with one left and
    // `show_ui` shows the window and applies the picked option. 0 with the setting off.
    int8_t Has_Pending(int16_t player, bool show_ui);

    void Clear();
    bool Empty();

    // Save section 'CHOI' (game/rules_sections.h): payload version 1, then the count (uint8), per record
    // module, kind (uint8), player (int8), target and other (int16), value and asked (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace choices

// The window (src/game/choice_box.cpp): the title, the body, then the options as lines with the hotkeys 1 to 3.
// Returns the index of the option picked.
namespace GENDRAW {
    int16_t Choice_Box(const choices::s_text& text);
} // namespace GENDRAW
