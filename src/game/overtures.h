#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Overtures: four events of diplomacy (docs/LocalLLM/events-catalog.md 3.5 and 3.12; README.md 10.9, wave 4):
// an outstretched hand (scientists of all races offer help for a smaller fleet), a leaked discovery (share it,
// sell it or suppress it), a mediator (a third power makes peace between two warring empires) and a paid
// offensive of the coalition (a computer at war pays an ally to fight the common enemy). The first two are
// one-off effects; the last two are kept as records. A human-owned subject takes the first, safest option (no
// interface step yet) and is never driven by the mediator or the paid offensive (only computers are); a
// computer picks by its personality (Choose).
// The mediator is the row "Посредник" of mechanics.md 3.5.3 (wave 6, ab-review.md 1.4 and 4.3; it was built
// first, in wave 4, as "peacekeepers" with a patrol strike, which the author struck out on 25.11.2026: "патрули
// не делаем"). The row, in its own words:
//   "Посредник | третья империя мирит воюющих за услуги от обоих | услуги, `Declare_Peace`"
// and the author's decision (4.3): "при мире каждая сторона должна посреднику услугу (`intrigue` favours);
// удар «патрулей» по кораблям при провале убрать; переименовать в коде и документах в «Посредник»".
// Off by default (events overtures); with the flag off nothing here draws from game_random, nothing is kept.
namespace overtures {

    extern bool enabled;  // events overtures

    constexpr int32_t MAX_RECORDS = 16;
    // A new event starts by chance, in per mille a turn for each empire (one draw each; at most one starts a
    // turn in the whole galaxy). The kind is drawn among the four.
    constexpr int32_t EMPIRE_CHANCE = 6;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_HAND = 1,          // an outstretched hand; one-off
        KIND_LEAK = 2,          // a leaked discovery; one-off
        KIND_MEDIATOR = 3,      // record; player: the mediator, target and other: the warring empires (was "peacekeepers")
        KIND_OFFENSIVE = 4,     // record; player: the payer, target: the paid ally, other: the enemy, value: BC
        KIND_LAST = 4,
    };

    enum e_hand_option : uint8_t { HAND_ACCEPT = 0, HAND_BARGAIN = 1, HAND_SEIZE = 2 };
    enum e_leak_option : uint8_t { LEAK_SHARE = 0, LEAK_SELL = 1, LEAK_SUPPRESS = 2 };
    // Paid offensive: the size of the payment (the first is the smallest).
    enum e_offensive_option : uint8_t { OFFENSIVE_MODEST = 0, OFFENSIVE_STANDARD = 1, OFFENSIVE_GENEROUS = 2 };

    // Outstretched hand (3.5): the scientists of all races offer help for a smaller fleet. Accepting scraps
    // HAND_SHIPS of the cheapest ships and yields HAND_APPS unknown technology applications (the cheapest by
    // field cost); bargaining scraps one ship and yields one application plus HAND_BARGAIN_BC; seizing the
    // delegation costs nothing and yields HAND_LOOT BC, but every empire in contact remembers it (bad rumours,
    // HAND_SEIZE_OPINION). Needs more than HAND_SHIPS ships.
    constexpr int32_t HAND_SHIPS = 3;
    constexpr int32_t HAND_APPS = 2;
    constexpr int32_t HAND_BARGAIN_BC = 40;
    constexpr int32_t HAND_LOOT = 80;
    constexpr int32_t HAND_SEIZE_OPINION = -15;
    // Leaked discovery (3.5): an application the empire knows and an empire in contact lacks. Shared, every such
    // empire (at most LEAK_MAX_SHARE) gets it and remembers a service (LEAK_SHARE_OPINION); sold, the richest
    // one that can pay LEAK_PRICE BC buys it; suppressed, it costs LEAK_SUPPRESS_COST BC of counterintelligence
    // and nothing leaks.
    constexpr int32_t LEAK_MAX_SHARE = 3;
    constexpr int32_t LEAK_SHARE_OPINION = 10;
    constexpr int32_t LEAK_PRICE = 100;
    constexpr int32_t LEAK_SUPPRESS_COST = 30;
    // Mediator (3.5.3): a third computer power in good standing with two warring computer empires works for peace
    // for PEACE_TURNS. Each turn the chance of peace, in percent, is PEACE_BASE plus the war weariness of both
    // sides (each in percent, halved), at most PEACE_MAX. When peace comes (Declare_Peace) each side owes the
    // mediator a favour (intrigue::Owe, with `diplomacy favours`; "услуги от обоих") and remembers it gratefully
    // (PEACE_OPINION, both of the mediator). When the turns pass with the war still on, the mediator gives up:
    // nothing is struck, nothing is owed.
    constexpr int32_t PEACE_TURNS = 8;
    constexpr int32_t PEACE_BASE = 8;
    constexpr int32_t PEACE_MAX = 80;
    constexpr int32_t PEACE_OPINION = 8;
    // Paid offensive (3.12): the payer, at war, pays an ally (a computer in contact with both, not at war with
    // the payer, not allied to the enemy, not war-weary, liking the payer) PAYMENT_PERCENT[option] percent of
    // its treasury (at least PAYMENT_MIN BC). An ally not yet at war with the enemy declares war on it; one
    // already at war only takes the money (the engine has no hook to hand its AI a target: the closest
    // equivalent). After OFFENSIVE_TURNS the payer judges the deal: the ally still at war with the enemy is
    // remembered well (OFFENSIVE_OPINION), one that made peace on the money broke its word (-OFFENSIVE_OPINION).
    constexpr int32_t OFFENSIVE_TURNS = 12;
    constexpr int32_t PAYMENT_MIN = 40;
    constexpr int32_t PAYMENT_PERCENT[3] = {5, 10, 20};
    constexpr int32_t OFFENSIVE_OPINION = 12;

    struct s_record {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;
        int16_t target = -1;
        int16_t other = -1;
        int32_t started = 0;
        int32_t ends = 0;
        int8_t option = 0;
        int16_t value = 0;
    };

    struct s_state {
        int32_t count = 0;
        s_record record[MAX_RECORDS];
    };
    s_state* State();

    // The option a computer of `player` takes for `kind` by its personality; a human, or the flag off: 0.
    int32_t Choose(e_kind kind, int16_t player);

    // Starts the event of `kind` for `player` (the subject: the empire the event happens to; for the mediator
    // an empire at war, for the paid offensive the payer) and applies the option Choose gives (`option` >= 0
    // overrides it). Draws from game_random only to pick among several candidates. False when it cannot start.
    bool Start(e_kind kind, int16_t player, int32_t option = -1);

    // Once a turn after incidents::Turn() (nextturn.cpp): the records run and end; a new event may start.
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'OVTR' (game/rules_sections.h): payload version 1 (kind 3 is the mediator, once the peacekeepers), then the count (uint8), per record kind
    // (uint8), player (int8), target and other (int16), started and ends (int32), option (int8), value (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace overtures
