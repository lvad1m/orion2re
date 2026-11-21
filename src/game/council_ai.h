#pragma once

#include <cstdint>

// The galactic council in games without human players (self-play, all-AI games). The original
// never convenes it there (nextturn.cpp tests RUSS::all_ai_game), so such games could only end
// by conquest and a strategy aiming at the council could not be measured.
namespace council {

    // gameplay council_without_humans (default false). When true, a game without human players
    // holds the council on the usual schedule and conditions, without the council screen, and
    // the turn oracle stops recording at the first victory (an elected council, the last empire
    // standing or Antares captured) and prints it.
    extern bool meetings_without_humans;

    // The council without its screen: the same nominees (the two empires with the most votes),
    // members, vote order - and so the same draws from game_random - vote effects on relations
    // and the same two-thirds rule as Main_Council_Screen, with every empire voting as a computer
    // empire. An elected nominee wins as if the ruling were accepted: diplodef::council_winner and
    // s_player::won_council_vote are set. Returns the winner, or -1.
    int16_t Hold_Council_Without_Humans();

} // namespace council
