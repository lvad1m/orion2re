#include "pch.h"

#include "game/advisor_apply.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/advisor_race.h"
#include "game/diplomac.h"

#include <algorithm>

// Nothing in this file draws from game_random.
namespace advisor {
    namespace {
        constexpr uint8_t LAST_OBJECTIVE = PLAYER_OBJECTIVE_ECOLOGIST;

        // The directive of `player` when the spy mechanic is on and the directive is active and
        // aimed at `target`; nullptr otherwise.
        const s_directive* Spy_Directive_(uint8_t player, uint8_t target) {
            if (!enabled || !rules_spies) return nullptr;
            const s_directive* directive = Get_Directive(player);
            if (directive == nullptr || target == NO_PLAYER || directive->spy_target != target) return nullptr;
            return Is_Active(directive, game::current.Stardate()) ? directive : nullptr;
        }

        // The active directive of the living computer player `player` when the intel network is on.
        const s_directive* Intel_Directive_(uint8_t player) {
            if (!enabled || !rules_spies || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return nullptr;
            const s_player& p = game::current.players[player];
            if (p.eliminated != 0 || p.objectives == PLAYER_OBJECTIVE_HUMAN) return nullptr;
            const s_directive* directive = Get_Directive(player);
            return (directive != nullptr && Is_Active(directive, game::current.Stardate())) ? directive : nullptr;
        }

        // Living empires `player` has contact with and may spy on (no "stop spying" agreement), the
        // directive's spy target first, then by lowest relations, ties by lower player number.
        int32_t Spy_Contacts_(uint8_t player, const s_directive* directive, uint8_t* out) {
            const s_player& p = game::current.players[player];
            int32_t n = 0;
            const int32_t players = game::current.Player_Count() < MAX_PLAYERS ? game::current.Player_Count() : MAX_PLAYERS;
            for (int32_t i = 0; i < players; ++i) {
                if (i == player || p.contact[i] == 0 || p.stop_spying_duration[i] != 0) continue;
                if (game::current.players[i].eliminated != 0) continue;
                out[n++] = static_cast<uint8_t>(i);
            }
            const uint8_t target = directive->spy_target;
            std::sort(out, out + n, [&](uint8_t a, uint8_t b) {
                if ((a == target) != (b == target)) return a == target;
                if (p.relations[a] != p.relations[b]) return p.relations[a] < p.relations[b];
                return a < b;
            });
            return n;
        }

    } // namespace

    void Apply_Directives(const game::s_view* world) {
        if (!enabled || !rules_objectives || world == nullptr || world->players == nullptr) return;
        const int32_t stardate = world->Stardate();
        int32_t player_count = world->Player_Count();
        if (player_count > MAX_PLAYERS) player_count = MAX_PLAYERS;

        for (int32_t i = 0; i < player_count; i++) {
            // The view's pointer is to non-const players; the pointer to the view is what is const.
            s_player* player = &world->players[i];
            if (player->eliminated != 0 || player->objectives == PLAYER_OBJECTIVE_HUMAN) continue;
            const uint8_t index = (uint8_t)i;

            const s_directive* directive = Get_Directive(index);
            if (directive == nullptr || !Is_Active(directive, stardate)) continue;
            if (directive->objective == OBJECTIVE_KEEP || directive->objective > LAST_OBJECTIVE) continue;
            if (directive->objective == player->objectives) continue;

            const int32_t last = Last_Objective_Change(index);
            if (last != 0 && stardate - last < objective_min_turns) continue;

            player->objectives = directive->objective;
            Set_Last_Objective_Change(index, stardate);
        }
    }

    uint32_t Spy_Target_Weight_Percent(uint8_t player, uint8_t target) {
        if (Spy_Directive_(player, target) == nullptr) return 100;
        return (uint32_t)spy_target_weight_percent;
    }

    int32_t Colonization_Push_Ships(uint8_t player) {
        if (!Colonization_Push(player)) return 0;
        const s_directive* directive = Get_Directive(player);
        return directive->weights[WEIGHT_COLONIZATION] >= LEVEL_MAX ? 2 : 1;
    }

    bool Outpost_Push(uint8_t player) {
        if (!enabled || !rules_outposts || player >= MAX_PLAYERS) return false;
        if (player >= game::current.Player_Count()) return false;
        const s_player& p = game::current.players[player];
        if (p.eliminated != 0 || p.objectives == PLAYER_OBJECTIVE_HUMAN) return false;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return false;
        const uint8_t level = directive->weights[WEIGHT_COLONIZATION];
        return level >= LEVEL_HIGH && level < LEVEL_COUNT;
    }

    bool Colonization_Push(uint8_t player) {
        if (!enabled || !rules_expansion || player >= MAX_PLAYERS) return false;
        if (player >= game::current.Player_Count()) return false;
        const s_player& p = game::current.players[player];
        if (p.eliminated != 0 || p.objectives == PLAYER_OBJECTIVE_HUMAN) return false;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return false;
        const uint8_t level = directive->weights[WEIGHT_COLONIZATION];
        return level >= LEVEL_HIGH && level < LEVEL_COUNT;
    }

    bool Desired_Spies(uint8_t player, uint8_t* count) {
        const s_directive* directive = Intel_Directive_(player);
        if (directive == nullptr || count == nullptr) return false;
        const s_player& p = game::current.players[player];
        uint8_t contacts[MAX_PLAYERS];
        const int32_t contact_count = Spy_Contacts_(player, directive, contacts);
        int32_t wanted = contact_count;
        const uint8_t target = directive->spy_target;
        if (target < MAX_PLAYERS && target != player && p.contact[target] != 0) {
            const int32_t treaty = p.treaty[target];
            wanted += ((treaty >= 4 && treaty <= 6) ? DESIRED_SPIES_AT_WAR : DESIRED_SPIES_AT_PEACE) - 1;
        }
        int32_t owned = 0;
        for (int16_t i = 0; i < game::current.Player_Count() && i < MAX_PLAYERS; ++i) {
            owned += BILL::Get_Their_Spy_Number(player, i);
        }
        const int32_t affordable = owned + (p.surplus_bc > 0 ? p.surplus_bc / BC_INCOME_PER_DESIRED_SPY : 0);
        int32_t desired = wanted < affordable ? wanted : affordable;
        if (desired > 63) desired = 63;
        *count = static_cast<uint8_t>(desired);
        return true;
    }

    void Cover_Contacts_With_Spies(uint8_t player) {
        const s_directive* directive = Intel_Directive_(player);
        if (directive == nullptr) return;
        uint8_t contacts[MAX_PLAYERS];
        const int32_t contact_count = Spy_Contacts_(player, directive, contacts);
        const int16_t players = static_cast<int16_t>(game::current.Player_Count() < MAX_PLAYERS ? game::current.Player_Count() : MAX_PLAYERS);
        for (int32_t k = 0; k < contact_count; ++k) {
            const uint8_t contact = contacts[k];
            if (BILL::Get_Their_Spy_Number(player, contact) > 0) continue;
            // Donor: the foreign target with the most spies (at least two; ties: lower index), else home.
            int16_t donor = -1;
            int16_t donor_spies = 1;
            for (int16_t i = 0; i < players; ++i) {
                if (i == player) continue;
                const int16_t spies = BILL::Get_Their_Spy_Number(player, i);
                if (spies > donor_spies) {
                    donor = i;
                    donor_spies = spies;
                }
            }
            if (donor < 0 && BILL::Get_Their_Spy_Number(player, player) >= 2) donor = player;
            if (donor < 0) return;
            BILL::Set_Their_Spy_Number(player, donor, static_cast<int16_t>(BILL::Get_Their_Spy_Number(player, donor) - 1));
            BILL::Set_Their_Spy_Number(player, contact, 1);
            int8_t mission = SPY_MISSION_HIDE;
            if (contact == directive->spy_target) {
                mission = 0;
                Preferred_Spy_Mission(player, contact, &mission);
            }
            BILL::Set_Their_Spy_Mission(player, contact, mission);
        }
    }

    int32_t Lanchester_Percent(uint8_t player, uint8_t other) {
        if (!enabled || !rules_lanchester || player >= MAX_PLAYERS || other >= MAX_PLAYERS) return 100;
        if (player >= game::current.Player_Count() || other >= game::current.Player_Count()) return 100;
        if (((active_players >> player) & 1) == 0) return 100;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 100;
        int32_t warships[2] = {0, 0};
        for (int32_t i = 0; i < game::current.Ship_Count(); ++i) {
            const s_ship_data& ship = game::current.ships[i];
            if (ship.d.ship_type != SHIP_TYPE_COMBAT || ship.status < 0 || ship.status > 2) continue;
            if (ship.owner == static_cast<int8_t>(player)) ++warships[0];
            if (ship.owner == static_cast<int8_t>(other)) ++warships[1];
        }
        const int32_t percent = 100 * (warships[0] + 1) / (warships[1] + 1);
        return std::clamp(percent, LANCHESTER_MIN_PERCENT, LANCHESTER_MAX_PERCENT);
    }

    int32_t Attack_Margin_Percent(uint8_t player) {
        if (!enabled || !rules_lanchester || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return 100;
        if (((active_players >> player) & 1) == 0) return 100;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 100;
        return DECISIVE_FORCE_PERCENT;
    }

    int16_t Design_Reference_Enemy(uint8_t player, int16_t fallback) {
        if (!enabled || !rules_counter_design || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return fallback;
        if (((active_players >> player) & 1) == 0) return fallback;
        const s_player& p = game::current.players[player];
        if (p.objectives == PLAYER_OBJECTIVE_HUMAN) return fallback;
        const int32_t players = game::current.Player_Count() < MAX_PLAYERS ? game::current.Player_Count() : MAX_PLAYERS;
        const auto living_contact = [&](int32_t other) {
            return other >= 0 && other < players && other != player && p.contact[other] != 0 &&
                game::current.players[other].eliminated == 0;
        };
        const s_directive* directive = Get_Directive(player);
        if (directive != nullptr && Is_Active(directive, game::current.Stardate()) && living_contact(directive->main_rival)) {
            return directive->main_rival;
        }
        int16_t best = -1;
        for (int32_t other = 0; other < players; ++other) {
            if (!living_contact(other)) continue;
            if (best < 0 || game::current.players[other].total_ship_strength[player] >
                                game::current.players[best].total_ship_strength[player]) {
                best = static_cast<int16_t>(other);
            }
        }
        return best >= 0 ? best : fallback;
    }

    int32_t Attitude_Bias(uint8_t player, uint8_t target) {
        if (!enabled || (!rules_balance && !rules_doctrine && !rules_phases) || player >= MAX_PLAYERS || target >= MAX_PLAYERS) return 0;
        if (player >= game::current.Player_Count()) return 0;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 0;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return 0;
        switch (directive->attitude[target]) {
            case ATTITUDE_ALLY: return 100;
            case ATTITUDE_FRIENDLY: return 50;
            case ATTITUDE_NEUTRAL: return Doctrine(DOCTRINE_PART_PEACE) ? DOCTRINE_PEACE_BIAS : 0;
            case ATTITUDE_WARY: return -25;
            case ATTITUDE_HOSTILE: return -50;
            case ATTITUDE_WAR: return -100;
            default: return 0;
        }
    }

    bool Readiness(uint8_t player) {
        if (!enabled || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return false;
        if (((active_players >> player) & 1) == 0) return false;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return false;
        if (Doctrine(DOCTRINE_PART_READINESS)) return true;
        // Phase strategy: a leader raising its army (rule 406) or a coalition at war (408) asks for
        // the fleet weight.
        if (!Phase(PHASE_PART_LEADER) && !Phase(PHASE_PART_COALITION)) return false;
        const s_directive* directive = Get_Directive(player);
        return directive != nullptr && Is_Active(directive, game::current.Stardate()) &&
            directive->weights[WEIGHT_FLEET] >= LEVEL_HIGH;
    }

    namespace {
        // Rule 408's joint war: `target` is the WAR main rival of the advised computer empire
        // `player`, and another living empire is at war with `target` too.
        bool Joint_War_(uint8_t player, uint8_t target) {
            if (!enabled || !Phase(PHASE_PART_COALITION) || player >= MAX_PLAYERS || target >= MAX_PLAYERS || player == target) return false;
            const int32_t players = std::min<int32_t>(game::current.Player_Count(), MAX_PLAYERS);
            if (player >= players || target >= players) return false;
            if (((active_players >> player) & 1) == 0) return false;
            if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return false;
            const s_directive* directive = Get_Directive(player);
            if (directive == nullptr || !Is_Active(directive, game::current.Stardate()) ||
                directive->main_rival != target || directive->attitude[target] != ATTITUDE_WAR) {
                return false;
            }
            for (int32_t k = 0; k < players; ++k) {
                if (k == player || k == target || game::current.players[k].eliminated != 0) continue;
                const int8_t treaty = game::current.players[k].treaty[target];
                if (treaty >= 4 && treaty <= 6) return true;
            }
            return false;
        }
    } // namespace

    int32_t Coalition_Percent(uint8_t player, uint8_t target) {
        if (!Joint_War_(player, target)) return 100;
        const int32_t players = std::min<int32_t>(game::current.Player_Count(), MAX_PLAYERS);
        const int64_t ours = game::current.players[player].total_ship_strength[target];
        int64_t partners = 0;
        for (int32_t k = 0; k < players; ++k) {
            if (k == player || k == target || game::current.players[k].eliminated != 0) continue;
            const int8_t treaty = game::current.players[k].treaty[target];
            if (treaty >= 4 && treaty <= 6) partners += game::current.players[k].total_ship_strength[target];
        }
        const int64_t percent = 100 * (ours + partners) / std::max<int64_t>(1, ours);
        return static_cast<int32_t>(std::clamp<int64_t>(percent, 100, COALITION_MAX_PERCENT));
    }

    int32_t Attack_Margin_Percent_Vs(uint8_t player, uint8_t target) {
        return Joint_War_(player, target) ? 100 : Attack_Margin_Percent(player);
    }

    int32_t Coalition_Worthiness_Percent(uint8_t player, uint8_t target) {
        return Joint_War_(player, target) ? COALITION_TARGET_PERCENT : 100;
    }

    int32_t Invasion_Transports(uint8_t player) {
        if (!enabled || !Phase(PHASE_PART_JOIN_LAST) || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return 0;
        if (((active_players >> player) & 1) == 0) return 0;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 0;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return 0;
        return directive->weights[WEIGHT_FLEET] >= LEVEL_MAX ? INVASION_TRANSPORTS : 0;
    }

    namespace {
        // s_player::treaty value of an alliance (advisor_view.cpp Treaty_Name).
        constexpr int8_t FRAME_ALLIANCE_TREATY_ = 2;

        bool Steered_Computer_Now_(uint8_t player) {
            if (!enabled || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return false;
            if (((active_players >> player) & 1) == 0) return false;
            const s_player& p = game::current.players[player];
            return p.eliminated == 0 && p.objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        // `offerer` offers `target` tribute for peace: rule VI with an active MAKE_PEACE directive.
        bool Offers_Tribute_(uint8_t offerer, uint8_t target) {
            if (!Doctrine(DOCTRINE_PART_TRIBUTE) || !Steered_Computer_Now_(offerer) || target >= MAX_PLAYERS) return false;
            const s_directive* directive = Get_Directive(offerer);
            return directive != nullptr && Is_Active(directive, game::current.Stardate()) &&
                directive->posture == POSTURE_MAKE_PEACE && directive->main_rival == target;
        }
    } // namespace

    int32_t Tribute_Peace_Turns(uint8_t evaluator, uint8_t offerer) {
        return evaluator < MAX_PLAYERS && Offers_Tribute_(offerer, evaluator) ? TRIBUTE_PEACE_TURNS : 0;
    }

    void After_Peace(uint8_t a, uint8_t b) {
        if (a >= MAX_PLAYERS || b >= MAX_PLAYERS || a == b) return;
        uint8_t payer = MAX_PLAYERS;
        uint8_t receiver = MAX_PLAYERS;
        if (Offers_Tribute_(a, b)) {
            payer = a;
            receiver = b;
        } else if (Offers_Tribute_(b, a)) {
            payer = b;
            receiver = a;
        }
        if (payer >= MAX_PLAYERS) return;
        diplomac::Start_Tribute_Treaty(payer, receiver, TRIBUTE_PERCENT_UNITS);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "advisor: stardate %d, player %d buys peace with player %d by tribute",
            (int)game::current.Stardate(), (int)payer, (int)receiver);
    }

    void Review_Tributes() {
        if (!enabled || !Doctrine(DOCTRINE_PART_TRIBUTE) || game::current.players == nullptr) return;
        if (game::current.Stardate() % std::max<int32_t>(1, rules_period) != 0) return;
        const int32_t players = std::min<int32_t>(game::current.Player_Count(), MAX_PLAYERS);
        for (int32_t payer = 0; payer < players; ++payer) {
            if (!Steered_Computer_Now_(static_cast<uint8_t>(payer))) continue;
            for (int32_t receiver = 0; receiver < players; ++receiver) {
                const s_player& p = game::current.players[payer];
                if (receiver == payer || p.tribute_treaty[receiver] == 0) continue;
                const int64_t ours = p.total_ship_strength[receiver];
                const int64_t theirs = game::current.players[receiver].total_ship_strength[payer];
                if (100 * ours >= TRIBUTE_STOP_PERCENT * std::max<int64_t>(1, theirs)) {
                    diplomac::Break_Tribute(static_cast<int16_t>(payer), static_cast<int16_t>(receiver));
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "advisor: stardate %d, player %d stops its tribute to player %d",
                        (int)game::current.Stardate(), payer, receiver);
                }
            }
        }
    }

    int16_t Frame_Choice(uint8_t actor, uint8_t victim, uint32_t candidates, int16_t original) {
        if (!Doctrine(DOCTRINE_PART_DIVIDE) || !Steered_Computer_Now_(actor) || victim >= MAX_PLAYERS) return original;
        const int32_t players = std::min<int32_t>(game::current.Player_Count(), MAX_PLAYERS);
        const s_player& self = game::current.players[actor];
        const auto allowed = [&](int32_t i) {
            return i >= 0 && i < players && i != actor && i != victim && ((candidates >> i) & 1) != 0 &&
                self.treaty[i] != FRAME_ALLIANCE_TREATY_;
        };
        const s_directive* directive = Get_Directive(actor);
        int16_t choice = -1;
        if (directive != nullptr && Is_Active(directive, game::current.Stardate()) && directive->main_rival < MAX_PLAYERS &&
            allowed(directive->main_rival)) {
            choice = directive->main_rival;
        }
        if (choice < 0 && Phase(PHASE_PART_COUNCIL_VOTES)) {
            const auto votes = [](const s_player& p) { return p.total_pop > 0 ? (p.total_pop + 9) / 10 : 0; };  // council.cpp Get_Num_Votes
            int32_t leader = -1;
            for (int32_t i = 0; i < players; ++i) {
                if (i == actor || game::current.players[i].eliminated != 0) continue;
                if (leader < 0 || votes(game::current.players[i]) > votes(game::current.players[leader])) leader = i;
            }
            if (leader >= 0 && votes(game::current.players[leader]) >= votes(self) && allowed(leader)) {
                choice = static_cast<int16_t>(leader);
            }
        }
        if (choice < 0) return original;
        if (choice != original) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "advisor: stardate %d, player %d frames player %d at player %d",
                (int)game::current.Stardate(), (int)actor, (int)choice, (int)victim);
        }
        return choice;
    }

    int32_t Council_Vote_Bias(uint8_t voter, uint8_t candidate) {
        return Phase(PHASE_PART_COUNCIL_VOTES) ? Attitude_Bias(voter, candidate) : 0;
    }

    int32_t Raid_Worthiness_Percent(uint8_t player, bool undefended) {
        if (!undefended || !enabled || !Doctrine(DOCTRINE_PART_RAIDS) || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return 100;
        if (((active_players >> player) & 1) == 0) return 100;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 100;
        // Race doctrine: stealthy races strike where the enemy is not, thrice rather than twice.
        if (rules_race && Race_Leanings(game::current.players[player].traits).stealth >= 1) return STEALTH_RAID_PERCENT;
        return DOCTRINE_RAID_PERCENT;
    }

    int32_t Peace_Eagerness_Turns(uint8_t player, uint8_t target) {
        if (!enabled || !Doctrine(DOCTRINE_PART_BUY_PEACE) || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return 0;
        if (((active_players >> player) & 1) == 0) return 0;
        if (game::current.players[player].objectives == PLAYER_OBJECTIVE_HUMAN) return 0;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return 0;
        return (directive->posture == POSTURE_MAKE_PEACE && directive->main_rival == target) ? DOCTRINE_PEACE_TURNS : 0;
    }

    bool Range_Research_Push(uint8_t player) {
        if (!enabled || !rules_exploration || player >= MAX_PLAYERS || player >= game::current.Player_Count()) return false;
        if (((active_players >> player) & 1) == 0) return false;
        const s_player& p = game::current.players[player];
        if (p.eliminated != 0 || p.objectives == PLAYER_OBJECTIVE_HUMAN) return false;
        const s_directive* directive = Get_Directive(player);
        return directive != nullptr && Is_Active(directive, game::current.Stardate()) &&
            directive->research_focus == RESEARCH_PROPULSION;
    }

    bool Is_Range_Application(int16_t tech_app) {
        switch (tech_app) {
            case TECH_APP_STANDARD_FUEL_CELLS:
            case TECH_APP_DEUTERIUM_FUEL_CELLS:
            case TECH_APP_IRIDIUM_FUEL_CELLS:
            case TECH_APP_URRIDIUM_FUEL_CELLS:
            case TECH_APP_THORIUM_FUEL_CELLS:
            case TECH_APP_EXTENDED_FUEL_TANKS:
                return true;
            default:
                return false;
        }
    }

    bool Preferred_Spy_Mission(uint8_t player, uint8_t target, int8_t* engine_mission) {
        const s_directive* directive = Spy_Directive_(player, target);
        if (directive == nullptr || engine_mission == nullptr) return false;
        if (directive->spy_mission == SPY_STEAL_TECH) {
            *engine_mission = 0;
            return true;
        }
        if (directive->spy_mission == SPY_SABOTAGE) {
            *engine_mission = 1;
            return true;
        }
        return false;
    }

} // namespace advisor
