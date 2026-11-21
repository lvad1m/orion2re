#pragma once

#include <cstdint>

#include "game/world_view.h"

// Applying directives to the game (stage 2, docs/LocalLLM/README.md section 10.2).
//
// Every effect is behind the master switch advisor::enabled AND its own mechanic flag
// (advisor::rules_objectives, advisor::rules_spies, advisor::rules_expansion). With either off the functions below are
// neutral: Apply_Directives changes nothing, the spy hooks return "unchanged". Nothing here
// draws from game_random; the spy hooks are placed so the number of game_random calls in
// spy.cpp stays the same whatever they return.
namespace advisor {

    // Called by Run_Turn after directives are stored. For every living computer player with an
    // active directive: objective (if not OBJECTIVE_KEEP and different from the current one)
    // is written to s_player::objectives, at most once per `objective_min_turns` turns
    // (Last_Objective_Change / Set_Last_Objective_Change), never for human players and never
    // to PLAYER_OBJECTIVE_HUMAN.
    void Apply_Directives(const game::s_view* world);

    // Spy target weight multiplier in percent for spy.cpp Allocate_AI_Spies: returns
    // `spy_target_weight_percent` when `player` has an active directive whose spy_target is
    // `target`, otherwise 100.
    uint32_t Spy_Target_Weight_Percent(uint8_t player, uint8_t target);

    // Mission override for the spies `player` sends to `target`: true and the engine mission
    // value (0 = espionage/steal technology, 1 = sabotage) when the active directive targets
    // `target` with SPY_STEAL_TECH or SPY_SABOTAGE; false = keep the engine's choice.
    bool Preferred_Spy_Mission(uint8_t player, uint8_t target, int8_t* engine_mission);

    // Stage 2v (README.md section 10.3): colonization push for the AI production planner.
    // True when `rules_expansion` is on and `player` is a living computer player whose active
    // directive has the colonization weight at LEVEL_HIGH or above. While true the planner
    // (ai_empire_production.cpp, aidudes.cpp) keeps one colony ship in production at its most
    // productive colony and does not scrap a colony ship under construction.
    bool Colonization_Push(uint8_t player);

    // How many colony ships the push keeps in production or on their way at once: 0 without the
    // push, 1 with the colonization weight HIGH, 2 with MAX (rule 301 with 3+ known free planets).
    int32_t Colonization_Push_Ships(uint8_t player);

    // Outposts (`rules_outposts`), with the same conditions as Colonization_Push: the AI keeps one
    // outpost ship in production or on its way while a free asteroid or gas giant is in reach
    // (ai_empire_production.cpp), sends it to the farthest such star within range (aimove.cpp) and
    // settles it there (colonize.cpp AI_Colonize).
    bool Outpost_Push(uint8_t player);

    constexpr int16_t OUTPOST_PUSH_MIN_INDUSTRY_WORTH = 6;

    // Minimum AIBUILD::Industry_Worth of the colony that starts the pushed colony ship.
    constexpr int16_t COLONIZATION_PUSH_MIN_INDUSTRY_WORTH = 10;

    // Stage 2b, intel network (docs/LocalLLM/mechanics.md 3.2.6, lever 2). With `rules_spies` on and
    // an active directive for the computer player `player`: true, and `*count` = spies it should
    // own in total: one observer for every contact it may spy on, plus DESIRED_SPIES_AT_WAR - 1 or
    // DESIRED_SPIES_AT_PEACE - 1 more at the directive's spy target, but never more than it owns now
    // plus one per BC_INCOME_PER_DESIRED_SPY BC of net income (spies cost 1 BC a turn each).
    // AIBUILD::Compute_Needs uses it instead of the original "as many as the neighbours" demand.
    // False: no advisor demand, the original AI decides.
    bool Desired_Spies(uint8_t player, uint8_t* count);

    // Called at the end of spy::Allocate_AI_Spies. With the same conditions as Desired_Spies,
    // moves single spies from the target holding the most (at least two), or from home when it
    // holds at least two, to every uncovered contact: the directive's spy target first, then by
    // lowest relations. Observers outside the spy target get the "hide" mission (2): they steal
    // nothing and are caught less often. No random draws.
    void Cover_Contacts_With_Spies(uint8_t player);

    constexpr int8_t SPY_MISSION_HIDE = 2;

    // Stage 2g (README.md 10.5, method 1): multiplier in percent for diplomac::
    // Fleet_Strength_Comparison(player, other) when `player` is an advised computer empire and
    // `rules_lanchester` is on: (warships of player + 1) / (warships of other + 1), clamped to
    // LANCHESTER_MIN_PERCENT..LANCHESTER_MAX_PERCENT. 100 otherwise. Warships: active combat ships.
    int32_t Lanchester_Percent(uint8_t player, uint8_t other);

    constexpr int32_t LANCHESTER_MIN_PERCENT = 25;

    // Decisive force (Lanchester): the space strength an advised computer empire wants, in percent
    // of the defending space strength, before it attacks a colony (aimove.cpp
    // Compute_Attack_Worthiness). Under the square law a 1.1:1 win leaves about 30 % of the fleet
    // and a 1.5:1 win about 75 %. 100 (attack with any edge, the original rule) unless
    // `rules_lanchester` is on for an advised computer empire.
    int32_t Attack_Margin_Percent(uint8_t player);

    constexpr int32_t DECISIVE_FORCE_PERCENT = 150;
    constexpr int32_t LANCHESTER_MAX_PERCENT = 400;

    // Stage 2d (README.md 10.5, method 2): score bias from the attitude of `player`'s active
    // directive toward `target`, for the AI's war, peace and treaty decisions (npcdiplo.cpp,
    // diplomac.cpp Diplomacy_Test). ALLY +100, FRIENDLY +50, WARY -25, HOSTILE -50, WAR -100,
    // NEUTRAL DOCTRINE_PEACE_BIAS with the doctrine's peace part (rule 404: a neighbour costly to
    // fight), otherwise 0; 0 unless `rules_balance` or `rules_doctrine` is on and `player` is an
    // advised computer empire.
    int32_t Attitude_Bias(uint8_t player, uint8_t target);

    // Council vote (council.cpp Vote_Check) of the computer empire `voter` for `candidate`, outside
    // war and alliance: with the council-votes part of `rules_phases` on, the directive's attitude moves the vote score like
    // Attitude_Bias (an empire resisting a would-be winner votes against it); 0 otherwise.
    int32_t Council_Vote_Bias(uint8_t voter, uint8_t candidate);

    // Byzantine rule I (doctrine peace part): the bias toward peace with a neighbour that would be
    // costly to fight (rule 404 sets attitude NEUTRAL).
    constexpr int32_t DOCTRINE_PEACE_BIAS = 20;

    // Byzantine rule I (doctrine readiness part): true for an advised computer empire, which then
    // sizes its fleet like a militarist - to the strongest neighbour, not the average
    // (aibuild.cpp, combat strength target). Threat-proportional: weak neighbours ask little.
    bool Readiness(uint8_t player);

    // Turns by which an advised empire's AI-to-AI peace comes sooner (npcdiplo.cpp
    // NPC_Peace_Negotiations): DOCTRINE_PEACE_TURNS toward the main rival when the active
    // directive's posture is MAKE_PEACE (rule VI, buy peace when losing), otherwise 0.
    int32_t Peace_Eagerness_Turns(uint8_t player, uint8_t target);

    constexpr int32_t DOCTRINE_PEACE_TURNS = 30;

    // Byzantine rule IV (raids, not battles): attack worthiness in percent for an advised empire's
    // attack on a colony with no defending space strength (aimove.cpp Compute_Attack_Worthiness):
    // DOCTRINE_RAID_PERCENT with `rules_doctrine` on, otherwise 100.
    int32_t Raid_Worthiness_Percent(uint8_t player, bool undefended);

    constexpr int32_t DOCTRINE_RAID_PERCENT = 200;

    // Ship design (aidesign.cpp): the empire whose shields, drives and weapons an advised computer
    // empire designs against. With `rules_counter_design` on: the directive's main rival when it is
    // a living contact, otherwise the living contact with the strongest fleet against `player`
    // (s_player::total_ship_strength), ties by lower index. `fallback` (the original choice, the
    // first living player) when off, for human players, players outside active_players, or without
    // contacts.
    int16_t Design_Reference_Enemy(uint8_t player, int16_t fallback);

    // Rule 501 hook for newtech::Choose_Tech_Application: true when `rules_exploration` is on and
    // the advised computer player `player` has an active directive with research focus PROPULSION.
    // The engine then gives range applications (fuel cells, extended fuel tanks) five times their
    // value, even before the basic fields are researched.
    bool Range_Research_Push(uint8_t player);

    // Fuel cells and extended fuel tanks: the applications that extend ship range.
    bool Is_Range_Application(int16_t tech_app);

    constexpr int32_t DESIRED_SPIES_AT_PEACE = 2;
    constexpr int32_t DESIRED_SPIES_AT_WAR = 4;
    constexpr int32_t BC_INCOME_PER_DESIRED_SPY = 5;

} // namespace advisor
