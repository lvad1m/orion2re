#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Vassals and tribute within the classic tribute treaty (docs/LocalLLM/mechanics.md 3.1 and 1.4;
// README.md 10.9, the A and B scope): a weak computer empire in danger (at war, or beside a hostile
// stronger neighbour) asks the strongest computer empire that can reach it and does not dislike it
// for protection; it pays VASSAL_TRIBUTE percent of its income as a classic tribute
// (Start_Tribute_Treaty), and when anyone else attacks it, the overlord goes to war with the
// aggressor - or, too weary or bound to the aggressor, fails it and the vassal goes free. The
// vassal goes free too when it grows to two thirds of the overlord's fleet, when they fight, or
// when the overlord attacks it. Only computer empires for now; the human gets it with the diplomacy
// screen. Off by default; nothing here draws from game_random (Declare_War does when it runs).
namespace vassals {

    extern bool enabled;   // diplomacy vassals

    constexpr int32_t VASSAL_TRIBUTE = 10;          // percent (classic tribute_treaty value)
    // Iteration 1, decision 4 (docs/LocalLLM/iterations.md): "Оживить редкие механики: подкуп совета, вассалы,
    // тайны, события дипломатии." Only the triggers are loosened: a neighbour is a danger when it is merely
    // as strong as we are (was: half again as strong, THREAT_STRENGTH_PERCENT 150 -> 100) and thinks of us
    // only a little ill (-30 -> -10); an empire up to half of the overlord's fleet may be taken (34 -> 50).
    constexpr int32_t WEAK_PERCENT = 50;            // the vassal's fleet against the overlord's, at most
    constexpr int32_t GROWN_PERCENT = 67;           // a vassal this strong goes free
    constexpr int32_t THREAT_RELATIONS = -10;       // a stronger neighbour this hostile is a danger
    constexpr int32_t THREAT_STRENGTH_PERCENT = 100;  // ... if its fleet is at least this percent of ours
    constexpr int32_t PROTECTED_OPINION = 15;       // opinion::REASON_OVERLORD on becoming a vassal and on a defence
    constexpr int32_t FAILED_OPINION = -30;         // an overlord that failed or attacked its vassal
    constexpr int32_t PERIOD = 10;

    // `vassal`'s overlord, -1 when none (or with the flag off).
    int16_t Overlord(int16_t vassal);
    bool Is_Vassal_Of(int16_t vassal, int16_t overlord);

    // Makes `vassal` the vassal of `overlord` (the tribute starts); false when it cannot be.
    bool Bind(int16_t vassal, int16_t overlord);
    // Ends `vassal`'s vassalage (the tribute stops) with a reason for the log.
    void Free(int16_t vassal, const char* why);

    // `aggressor` attacked `victim` (pacts.cpp On_Aggression, before the pacts answer): an overlord
    // defends its vassal or fails it; an overlord that attacks its own vassal frees it.
    void On_Aggression(int16_t aggressor, int16_t victim);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): every PERIOD turns vassalages are
    // reviewed and endangered weak empires seek an overlord.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'VASL' (game/rules_sections.h): payload version 1, then per player its overlord
    // (int8, -1 none) and the stardate the vassalage began (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    struct s_state {
        int8_t overlord[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        int32_t since[8] = {};
    };
    s_state* State();

} // namespace vassals
