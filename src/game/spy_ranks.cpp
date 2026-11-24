#include "pch.h"

#include "game/spy_ranks.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/bill.h"
#include "game/espionage.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/rivalry.h"
#include "game/spy.h"

#include <algorithm>

// The behaviour of counter-agents and spy ranks (mechanics.md 3.2.5.4); the state and its save
// section are in spy_ranks_state.cpp. game_random is drawn only by foils, catches and saboteurs.
namespace spy_ranks {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool Settled_Of_(int16_t colony, int16_t owner) {
            if (colony < 0 || colony >= MOX::NUM_COLONIES || colony >= MAX_COLONIES) return false;
            const s_colony& c = MOX::colony[colony];
            return c.owner == owner && c.outpost_flag == 0;
        }

        int32_t Spy_Defense_(int16_t player) {
            int16_t offense = 0;
            int16_t defense = 0;
            spy::Compute_Spy_Bonuses(&MOX::player[player], &offense, &defense);  // the second output is the defence
            return defense;
        }

        // The empire a resident goes to: our main rival, our declared rival, or the contact where
        // most of our spies work.
        int16_t Resident_Target_(int16_t owner) {
            if (advisor::enabled && ((advisor::active_players >> owner) & 1) != 0) {
                const advisor::s_directive* d = advisor::Get_Directive(static_cast<uint8_t>(owner));
                if (d != nullptr && advisor::Is_Active(d, MOX::stardate) && d->main_rival < MAX_PLAYERS && Alive_(d->main_rival) &&
                    MOX::player[owner].contact[d->main_rival] != 0) {
                    return d->main_rival;
                }
            }
            const int16_t rival = rivalry::Rival_Of(owner);
            if (rival >= 0 && Alive_(rival)) return rival;
            int16_t best = -1;
            int32_t best_spies = 0;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == owner || !Alive_(k) || MOX::player[owner].contact[k] == 0) continue;
                const int32_t spies = BILL::Get_Their_Spy_Number(owner, k);
                if (spies > best_spies) {
                    best = k;
                    best_spies = spies;
                }
            }
            return best;
        }

        // The enemy a saboteur goes to: the one at war with us that has the most colonies.
        int16_t Saboteur_Target_(int16_t owner) {
            int16_t best = -1;
            int32_t best_colonies = -1;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == owner || !Alive_(k) || MOX::player[owner].treaty[k] < TREATY_FIRST_WAR) continue;
                int32_t colonies = 0;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    if (MOX::colony[c].owner == k) ++colonies;
                }
                if (colonies > best_colonies) {
                    best = k;
                    best_colonies = colonies;
                }
            }
            return best;
        }

        void Saboteurs_Turn_() {
            s_state* s = State();
            for (int16_t owner = 0; owner < Players_(); ++owner) {
                if (s->saboteurs[owner] == 0 || !Alive_(owner)) continue;
                const int16_t target = Saboteur_Target_(owner);
                if (target < 0) continue;  // they wait for a war
                const int32_t attempts = std::min<int32_t>(s->saboteurs[owner], SABOTEUR_ATTEMPTS);
                s->saboteurs[owner] = static_cast<uint8_t>(s->saboteurs[owner] - attempts);
                const int32_t chance = SABOTEUR_PERCENT - Spy_Defense_(target) / 5;
                for (int32_t a = 0; a < attempts; ++a) {
                    if (static_cast<int32_t>(game_random::Random(100)) <= chance) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ranks: stardate %d, a saboteur of player %d strikes player %d",
                            (int)MOX::stardate, (int)owner, (int)target);
                        spy::Destroy_Random_Building(owner, target, owner);
                        reputation::Note_Deed(owner);  // stains and titles (off by default)
                    } else if (static_cast<int32_t>(game_random::Random(100)) <= CATCH_FOILED_PERCENT) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ranks: stardate %d, player %d catches a saboteur of player %d",
                            (int)MOX::stardate, (int)target, (int)owner);
                        opinion::Add(target, owner, opinion::REASON_SABOTAGE, SABOTEUR_OPINION);
                        espionage::On_Spy_Caught(target, owner);
                    }
                }
            }
        }

        void Computers_Buy_() {
            s_state* s = State();
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Computer_(p)) continue;
                s_player& player = MOX::player[p];
                if (counter_agents) {
                    int32_t total = 0;
                    for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                        if (MOX::colony[c].owner == p) total += s->counter_agents[c];
                    }
                    const int32_t price = 2 * counter_agent_cost;
                    if ((total + 1) / 2 <= std::max<int32_t>(0, player.surplus_bc) * 5 / 100 && player.bc >= price) {
                        int16_t colony = Settled_Of_(s->last_sabotaged[p], p) && s->counter_agents[s->last_sabotaged[p]] < MAX_PER_COLONY
                            ? s->last_sabotaged[p] : -1;
                        if (colony < 0) {  // no sabotage to answer: the most populous colony with room
                            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                                if (!Settled_Of_(c, p) || s->counter_agents[c] >= MAX_PER_COLONY) continue;
                                if (colony < 0 || MOX::colony[c].n_pops > MOX::colony[colony].n_pops) colony = c;
                            }
                        }
                        if (colony >= 0) {
                            player.bc -= price;
                            ++s->counter_agents[colony];
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ranks: stardate %d, player %d posts a counter-agent at colony %d",
                                (int)MOX::stardate, (int)p, (int)colony);
                        }
                    }
                }
                if (!ranks) continue;
                const int32_t saboteur_price = 2 * saboteur_cost;
                if (Saboteur_Target_(p) >= 0 && player.bc >= 4 * saboteur_price && s->saboteurs[p] < 255) {
                    player.bc -= saboteur_price;
                    ++s->saboteurs[p];
                }
                const int32_t resident_price = 2 * resident_cost;
                const int16_t target = Resident_Target_(p);
                // Computer treasuries rarely exceed a few hundred BC: the price itself is the bar.
                if (target >= 0 && player.treaty[target] < TREATY_FIRST_WAR && s->residents[p][target] == 0 &&
                    player.bc >= resident_price) {
                    player.bc -= resident_price;
                    s->residents[p][target] = 1;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ranks: stardate %d, player %d places a resident at player %d",
                        (int)MOX::stardate, (int)p, (int)target);
                }
            }
        }
    } // namespace

    bool Can_Build(const s_colony* colony, int16_t product) {
        if (colony == nullptr || !Alive_(colony->owner)) return false;
        if (MOX::player[colony->owner].tech_applications[TECH_APP_SPY_NETWORK] != TECH_RESEARCH_STATUS_RESEARCHED) return false;
        if (product == PRODUCT_COUNTER_AGENT) {
            const int16_t index = static_cast<int16_t>(colony - MOX::colony);
            return counter_agents && colony->outpost_flag == 0 && Counter_Agents(index) < MAX_PER_COLONY;
        }
        return ranks && (product == PRODUCT_SABOTEUR || product == PRODUCT_RESIDENT);
    }

    int32_t Cost(int16_t product) {
        switch (product) {
            case PRODUCT_COUNTER_AGENT: return counter_agent_cost;
            case PRODUCT_SABOTEUR: return saboteur_cost;
            case PRODUCT_RESIDENT: return resident_cost;
            default: return 0;
        }
    }

    const char* Name(int16_t product) {
        switch (product) {
            case PRODUCT_COUNTER_AGENT: return "Counter-Agent";
            case PRODUCT_SABOTEUR: return "Saboteur";
            case PRODUCT_RESIDENT: return "Resident";
            default: return "";
        }
    }

    const char* Description(int16_t product) {
        switch (product) {
            case PRODUCT_COUNTER_AGENT:
                return "Guards this colony: may foil enemy sabotage here and catch the spy. Up to 3 per colony.";
            case PRODUCT_SABOTEUR:
                return "Goes to an enemy at war with us and strikes once at a building, then is gone.";
            case PRODUCT_RESIDENT:
                return "Settles at our main rival: our spy operations there succeed more often, our intelligence is exact.";
            default: return "";
        }
    }

    bool On_Built(int16_t colony, int16_t product) {
        if (colony < 0 || colony >= MOX::NUM_COLONIES || colony >= MAX_COLONIES) return false;
        const int16_t owner = MOX::colony[colony].owner;
        if (!Alive_(owner)) return false;
        s_state* s = State();
        switch (product) {
            case PRODUCT_COUNTER_AGENT:
                if (!counter_agents || s->counter_agents[colony] >= MAX_PER_COLONY) return false;
                ++s->counter_agents[colony];
                return true;
            case PRODUCT_SABOTEUR:
                if (!ranks || s->saboteurs[owner] == 255) return false;
                ++s->saboteurs[owner];
                return true;
            case PRODUCT_RESIDENT: {
                const int16_t target = ranks ? Resident_Target_(owner) : -1;
                if (target < 0 || s->residents[owner][target] == 255) return false;
                ++s->residents[owner][target];
                return true;
            }
            default: return false;
        }
    }

    int32_t Capitol_Defenders(int16_t player) {
        if (!counter_agents || !Alive_(player)) return 0;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            const s_colony& colony = MOX::colony[c];
            if (colony.owner == player && colony.buildings[BUILDING_CAPITOL] != 0) return Counter_Agents(c) / 2;
        }
        return 0;
    }

    int32_t Upkeep(int16_t player) {
        if (!Alive_(player)) return 0;
        int32_t agents = 0;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            if (MOX::colony[c].owner == player) agents += Counter_Agents(c);
        }
        int32_t residents = 0;
        for (int16_t t = 0; t < MAX_PLAYERS; ++t) residents += Residents(player, t);
        return agents / 2 + 2 * residents;
    }

    bool Foil_Sabotage(int16_t actor, int16_t colony) {
        const int32_t agents = Counter_Agents(colony);
        if (agents == 0) return false;
        const int32_t chance = std::min<int32_t>(FOIL_MAX_PERCENT, agents * FOIL_PERCENT);
        if (static_cast<int32_t>(game_random::Random(100)) > chance) return false;
        const int16_t owner = MOX::colony[colony].owner;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ranks: stardate %d, counter-agents of player %d foil a sabotage at colony %d",
            (int)MOX::stardate, (int)owner, (int)colony);
        if (static_cast<int32_t>(game_random::Random(100)) <= CATCH_FOILED_PERCENT && actor >= 0 && actor < MAX_PLAYERS && actor != owner) {
            const int32_t spies = BILL::Get_Their_Spy_Number(actor, owner);
            if (spies > 0) BILL::Set_Their_Spy_Number(actor, owner, static_cast<uint8_t>(spies - 1));
            opinion::Add(owner, actor, opinion::REASON_SABOTAGE, SABOTEUR_OPINION);
            espionage::On_Spy_Caught(owner, actor);
        }
        return true;
    }

    void Note_Sabotage(int16_t colony) {
        if (colony < 0 || colony >= MOX::NUM_COLONIES || colony >= MAX_COLONIES) return;
        const int16_t owner = MOX::colony[colony].owner;
        if (owner >= 0 && owner < MAX_PLAYERS) State()->last_sabotaged[owner] = colony;
    }

    void Turn() {
        if (!counter_agents && !ranks) return;
        if (ranks) Saboteurs_Turn_();
        if (MOX::stardate % AI_PERIOD == 0) Computers_Buy_();
    }

} // namespace spy_ranks
