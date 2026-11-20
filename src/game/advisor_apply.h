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

    // Minimum AIBUILD::Industry_Worth of the colony that starts the pushed colony ship.
    constexpr int16_t COLONIZATION_PUSH_MIN_INDUSTRY_WORTH = 10;

} // namespace advisor
