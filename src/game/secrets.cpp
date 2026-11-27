#include "pch.h"

#include "game/secrets.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/council.h"
#include "game/counterspy.h"
#include "game/covenants.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/ambition.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/rivalry.h"
#include "game/spy.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"

#include <algorithm>

// The behaviour of secrets and smear campaigns (mechanics.md 3.5.2 and 3.2); the store and its save
// section are in secrets_state.cpp. game_random is drawn only with spy secrets or spy smear on.
namespace secrets {

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

        int32_t Spies_(int16_t owner, int16_t target) {
            return BILL::Get_Their_Spy_Number(owner, target);
        }

        bool Plans_(int16_t owner, int16_t victim) {
            int16_t victims[MAX_PLAYERS];
            const int32_t n = intrigue::Planned_Victims(owner, victims);
            for (int32_t i = 0; i < n; ++i) {
                if (victims[i] == victim) return true;
            }
            return false;
        }

        // The victim learns the secret: the same effects as its own spies uncovering it.
        void Tell_Victim_(const s_secret& s) {
            counterspy::On_Secret_Leaked(s.owner);  // the owner suspects a mole, sets a canary trap (off by default)
            const int16_t owner = s.owner;
            const int16_t victim = s.victim;
            if (s.kind == KIND_PLAN) {
                opinion::Add(victim, owner, opinion::REASON_PLOTTED_AGAINST_US, intrigue::PLOTTED_OPINION);
                s_player& p = MOX::player[owner];
                if (p.sneak_attack_planet != -1 && p.sneak_attack_player == victim) {
                    p.sneak_attack_planet = -1;
                    p.sneak_attack_player = -1;
                    p.war_buildup_duration = 0;
                }
            } else if (s.kind == KIND_BRIBE && s.value >= 0 && s.value < MAX_LEADERS) {
                *spy_ops::Bribed_Slot(s.value) = -1;
                opinion::Add(victim, owner, opinion::REASON_BRIBED_OUR_LEADER, -25);
            } else if (s.kind == KIND_TREATY) {
                covenants::Reveal(covenants::Find_Clause(owner, s.value, victim), "a secret told");
            } else if (s.kind == KIND_BIOWEAPON) {
                opinion::Add(victim, owner, opinion::REASON_ATROCITY, reputation::ATROCITY_OPINION);  // the victim now knows the culprit
            }
        }

        const char* Kind_Name_(uint8_t kind) {
            switch (kind) {
                case KIND_PLAN: return "a planned attack";
                case KIND_BRIBE: return "a bribed governor";
                case KIND_TREATY: return "a secret treaty";
                case KIND_BIOWEAPON: return "the use of biological weapons";
                default: return "a secret";
            }
        }

        bool Knows_(int16_t holder, e_kind kind, int16_t owner, int16_t victim, int16_t value) {
            for (int32_t k = 0; k < MAX_SECRETS; ++k) {
                const s_secret& s = Slots()[k];
                if (s.kind == kind && s.owner == owner && s.victim == victim && s.holder == holder && s.value == value) return true;
            }
            return false;
        }

        // The cheapest application `giver` knows and `taker` may get; -1 if none.
        int16_t Gift_App_(int16_t giver, int16_t taker) {
            s_player& from = MOX::player[giver];
            s_player& to = MOX::player[taker];
            int16_t best = -1;
            for (int16_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT; ++app) {
                if (from.tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED) continue;
                if (to.tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED || !COLCALC::Player_May_Get_Tech_App(&to, app)) continue;
                if (best < 0 || projects::App_Cost(app) < projects::App_Cost(best)) best = app;
            }
            return best;
        }

        void Learn_All_() {
            for (int16_t h = 0; h < Players_(); ++h) {
                if (!Computer_(h)) continue;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == h || !Alive_(o)) continue;
                    const int32_t chance = LEARN_PER_SPY * std::min(5, Spies_(h, o)) + LEARN_PER_RESIDENT * spy_ranks::Residents(h, o);
                    if (chance <= 0) continue;
                    // The first secret of `o` that `h` does not know yet, if any; roll only then.
                    e_kind kind = KIND_NONE;
                    int16_t victim = -1;
                    int16_t value = -1;
                    int16_t victims[MAX_PLAYERS];
                    const int32_t n = intrigue::Planned_Victims(o, victims);
                    for (int32_t i = 0; i < n && kind == KIND_NONE; ++i) {
                        if (victims[i] == h) continue;
                        bool known = false;
                        for (int32_t k = 0; k < MAX_SECRETS; ++k) {
                            const s_secret& s = Slots()[k];
                            known = known || (s.kind == KIND_PLAN && s.owner == o && s.victim == victims[i] && s.holder == h);
                        }
                        if (!known) {
                            kind = KIND_PLAN;
                            victim = victims[i];
                        }
                    }
                    for (int16_t l = 0; l < MAX_LEADERS && kind == KIND_NONE; ++l) {
                        const s_leader_data& leader = MOX::leaders[l];
                        if (spy_ops::Bribed_By(l) != o || leader.player_index == h || leader.player_index < 0) continue;
                        bool known = false;
                        for (int32_t k = 0; k < MAX_SECRETS; ++k) {
                            const s_secret& s = Slots()[k];
                            known = known || (s.kind == KIND_BRIBE && s.value == l && s.holder == h);
                        }
                        if (!known) {
                            kind = KIND_BRIBE;
                            victim = leader.player_index;
                            value = l;
                        }
                    }
                    // A secret treaty `o` guarantees (covenants.h), unknown to its target and not revealed yet.
                    const covenants::s_state* treaties = covenants::State();
                    for (int32_t c = 0; c < treaties->count && kind == KIND_NONE; ++c) {
                        const covenants::s_clause& clause = treaties->clauses[c];
                        if (clause.guarantor != o || clause.revealed != 0 || clause.target == h || clause.protege == h) continue;
                        if (Knows_(h, KIND_TREATY, o, clause.target, clause.protege)) continue;
                        kind = KIND_TREATY;
                        victim = clause.target;
                        value = clause.protege;
                    }
                    // A use of biological weapons by `o` (On_Bio_Weapon), against a colony of another empire.
                    const s_bio& deed = Bio()[o];
                    if (kind == KIND_NONE && deed.turn != 0 && deed.victim >= 0 && deed.victim != h && !Knows_(h, KIND_BIOWEAPON, o, deed.victim, -1)) {
                        kind = KIND_BIOWEAPON;
                        victim = deed.victim;
                    }
                    if (kind == KIND_NONE) continue;
                    if (static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
                    if (Learn(h, kind, o, victim, value)) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d learns a secret of player %d: %s against player %d",
                            (int)MOX::stardate, (int)h, (int)o, Kind_Name_(kind), (int)victim);
                    }
                }
            }
        }

        void Use_All_() {
            bool fresh[MAX_SECRETS] = {};  // copies sold this turn wait for the next one
            int16_t kompromat[MAX_PLAYERS];  // each holder's kompromat target in the pre-council window (-1: none)
            for (int16_t h = 0; h < MAX_PLAYERS; ++h) kompromat[h] = Kompromat_Target(h);
            for (int32_t i = 0; i < MAX_SECRETS; ++i) {
                s_secret& s = Slots()[i];
                if (s.kind == KIND_NONE || fresh[i]) continue;
                if (!Still_True(s) || !Computer_(s.holder) || !Alive_(s.owner) || !Alive_(s.victim)) {
                    s = s_secret{};
                    continue;
                }
                if (s.demanded != 0) continue;  // the owner pays with its council vote: kept for the vote
                // The kompromat rule: a secret of the nominee the holder opposes is made public before the session.
                if (kompromat[s.holder] >= 0 && kompromat[s.holder] == s.owner) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d aims its kompromat at council nominee %d",
                        (int)MOX::stardate, (int)s.holder, (int)s.owner);
                    Make_Public(i);
                    continue;
                }
                switch (MOX::player[s.holder].personality) {
                    case PLAYER_PERSONALITY_HONORABLE:
                    case PLAYER_PERSONALITY_PACIFIST:
                        if (!Sell(i)) Make_Public(i);
                        break;
                    case PLAYER_PERSONALITY_RUTHLESS:
                        Blackmail_For(i, DEMAND_TRIBUTE);
                        break;
                    case PLAYER_PERSONALITY_AGGRESSIVE:
                        Blackmail_For(i, DEMAND_TECH);
                        break;
                    case PLAYER_PERSONALITY_XENOPHOBIC:
                        Blackmail_For(i, DEMAND_VOTE);
                        break;
                    default: {  // erratic, dishonored: sell to whoever pays, else make it public
                        int32_t copy = -1;
                        if (Sell_To_Third(i, &copy)) {
                            fresh[copy] = true;
                        } else {
                            Make_Public(i);
                        }
                        break;
                    }
                }
            }
        }

        void Smear_All_() {
            for (int16_t a = 0; a < Players_(); ++a) {
                if (!Computer_(a)) continue;
                // The kompromat rule: before a council session the campaign goes against the nominee `a` opposes.
                const int16_t nominee = Kompromat_Target(a);
                if (nominee >= 0 && nominee != a && Alive_(nominee) && Spies_(a, nominee) != 0 && espionage::Points(a, nominee) >= smear_cost) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d aims its kompromat at council nominee %d",
                        (int)MOX::stardate, (int)a, (int)nominee);
                    Smear(a, nominee);
                    continue;
                }
                for (int16_t t = 0; t < Players_(); ++t) {
                    if (t == a || !Alive_(t) || Spies_(a, t) == 0 || espionage::Points(a, t) < smear_cost) continue;
                    const bool foe = MOX::player[a].treaty[t] >= TREATY_FIRST_WAR || rivalry::Rival_Of(a) == t || opinion::Relations(a, t) <= -30;
                    if (!foe) continue;
                    Smear(a, t);
                    break;  // one campaign a period
                }
            }
        }
    } // namespace

    int16_t Kompromat_Target(int16_t holder, int32_t lookahead) {
        if (!council_kompromat || !Computer_(holder) || !council::Session_Due_Within(KOMPROMAT_TURNS + lookahead)) return -1;
        uint8_t nominees[2];
        if (!council::Predict_Nominees(nominees)) return -1;
        for (int i = 0; i < 2; ++i) {
            if (nominees[i] == holder) return nominees[1 - i];
        }
        const bool against0 = council::Vote_Score(nominees[0], holder, nominees[1]) < KOMPROMAT_EVEN_SCORE;
        const bool against1 = council::Vote_Score(nominees[1], holder, nominees[0]) < KOMPROMAT_EVEN_SCORE;
        if (against0 && against1) {
            return council::Get_Num_Votes(nominees[1]) > council::Get_Num_Votes(nominees[0]) ? nominees[1] : nominees[0];
        }
        if (against0) return nominees[0];
        return against1 ? nominees[1] : -1;
    }

    bool Still_True(const s_secret& s) {
        if (s.kind == KIND_NONE || MOX::stardate - s.learned >= SECRET_TURNS) return false;
        if (s.kind == KIND_PLAN) return Plans_(s.owner, s.victim);
        if (s.kind == KIND_BRIBE) return s.value >= 0 && s.value < MAX_LEADERS && spy_ops::Bribed_By(s.value) == s.owner;
        if (s.kind == KIND_TREATY) {
            const int32_t clause = covenants::Find_Clause(s.owner, s.value, s.victim);
            return clause >= 0 && covenants::State()->clauses[clause].revealed == 0;
        }
        if (s.kind == KIND_BIOWEAPON) {
            const s_bio& deed = Bio()[s.owner];
            return deed.turn != 0 && deed.victim == s.victim;
        }
        return false;
    }

    void On_Bio_Weapon(int16_t owner, int16_t star) {
        if (!enabled || !Alive_(owner)) return;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            const s_colony& colony = MOX::colony[c];
            if (colony.owner == owner || !Alive_(colony.owner) || colony.planet < 0 || colony.planet >= MAX_PLANETS) continue;
            if (MOX::planet[colony.planet].star_index != star) continue;
            Bio()[owner].turn = MOX::stardate;
            Bio()[owner].victim = static_cast<int8_t>(colony.owner);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d used biological weapons against player %d",
                (int)MOX::stardate, (int)owner, (int)colony.owner);
            return;
        }
    }

    bool Sell(int32_t index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return false;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE || !Computer_(s.victim) || MOX::player[s.victim].bc < SALE_PRICE) return false;
        MOX::player[s.victim].bc -= SALE_PRICE;
        MOX::player[s.holder].bc += SALE_PRICE;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d sells player %d a secret of player %d: %s",
            (int)MOX::stardate, (int)s.holder, (int)s.victim, (int)s.owner, Kind_Name_(s.kind));
        Tell_Victim_(s);
        s = s_secret{};
        return true;
    }

    bool Blackmail(int32_t index) {
        return Blackmail_For(index, DEMAND_TRIBUTE);
    }

    bool Blackmail_For(int32_t index, e_demand demand) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return false;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE) return false;
        s_player& owner = MOX::player[s.owner];
        const bool refuses = owner.objectives == PLAYER_OBJECTIVE_HUMAN || owner.bc < BLACKMAIL_MIN_BC ||
            opinion::Relations(s.owner, s.holder) <= BLACKMAIL_REFUSE_RELATIONS;
        if (refuses) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d refuses to pay player %d for its silence",
                (int)MOX::stardate, (int)s.owner, (int)s.holder);
            Make_Public(index);
            return false;
        }
        if (demand == DEMAND_VOTE && ambition::council_bribery && Computer_(s.owner)) {
            // The silence is paid for in the council: the secret is kept (ambition.cpp spends it when
            // the holder stands as a nominee and the owner votes) until it goes stale.
            s.demanded = 1;
            opinion::Add(s.owner, s.holder, opinion::REASON_BLACKMAILED_US, BLACKMAIL_OPINION);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d demands the council vote of player %d for its silence about %s",
                (int)MOX::stardate, (int)s.holder, (int)s.owner, Kind_Name_(s.kind));
            return true;
        }
        if (demand == DEMAND_TECH) {
            const int16_t app = Gift_App_(s.owner, s.holder);
            if (app >= 0) {
                COLCALC::Player_Gets_Tech_App(&MOX::player[s.holder], app);
                opinion::Add(s.owner, s.holder, opinion::REASON_BLACKMAILED_US, BLACKMAIL_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d gives player %d the application %d for its silence about %s",
                    (int)MOX::stardate, (int)s.owner, (int)s.holder, (int)app, Kind_Name_(s.kind));
                s = s_secret{};
                return true;
            }
        }
        const int32_t price = owner.bc * BLACKMAIL_PERCENT / 100;
        owner.bc -= price;
        MOX::player[s.holder].bc += price;
        opinion::Add(s.owner, s.holder, opinion::REASON_BLACKMAILED_US, BLACKMAIL_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d pays player %d %d BC for its silence about %s",
            (int)MOX::stardate, (int)s.owner, (int)s.holder, (int)price, Kind_Name_(s.kind));
        s = s_secret{};
        return true;
    }

    void Make_Public(int32_t index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d makes public a secret of player %d: %s against player %d",
            (int)MOX::stardate, (int)s.holder, (int)s.owner, Kind_Name_(s.kind), (int)s.victim);
        Tell_Victim_(s);
        opinion::Add(s.owner, s.holder, opinion::REASON_EXPOSED_US, EXPOSED_OPINION);
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == s.owner || o == s.holder || o == s.victim || !Computer_(o) || MOX::player[o].contact[s.owner] == 0) continue;
            // Made public, the use of biological weapons is an atrocity in everyone's eyes.
            if (s.kind == KIND_BIOWEAPON) {
                opinion::Add(o, s.owner, opinion::REASON_ATROCITY, reputation::ATROCITY_OPINION);
            } else {
                opinion::Add(o, s.owner, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
            }
        }
        s = s_secret{};
    }

    bool Sell_To_Third(int32_t index, int32_t* new_index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return false;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE) return false;
        // The buyer: a computer empire that thinks ill of the owner (the most), is in contact with the
        // seller, and can pay in BC or, being poor, in a favour.
        int16_t buyer = -1;
        for (int16_t b = 0; b < Players_(); ++b) {
            if (b == s.owner || b == s.victim || b == s.holder || !Computer_(b) || MOX::player[b].contact[s.holder] == 0) continue;
            if (Knows_(b, static_cast<e_kind>(s.kind), s.owner, s.victim, s.value)) continue;
            if (opinion::Relations(b, s.owner) > THIRD_PARTY_MAX_RELATIONS) continue;
            if (MOX::player[b].bc < SALE_PRICE && !intrigue::favours) continue;
            if (buyer < 0 || opinion::Relations(b, s.owner) < opinion::Relations(buyer, s.owner)) buyer = b;
        }
        if (buyer < 0) return false;
        const s_secret sold = s;
        if (!Learn(buyer, static_cast<e_kind>(sold.kind), sold.owner, sold.victim, sold.value)) return false;
        for (int32_t k = 0; k < MAX_SECRETS; ++k) {  // the buyer's copy is as old as the original
            s_secret& copy = Slots()[k];
            if (copy.kind == sold.kind && copy.holder == buyer && copy.owner == sold.owner && copy.victim == sold.victim && copy.value == sold.value) {
                copy.learned = sold.learned;
                if (new_index != nullptr) *new_index = k;
            }
        }
        if (MOX::player[buyer].bc >= SALE_PRICE) {
            MOX::player[buyer].bc -= SALE_PRICE;
            MOX::player[sold.holder].bc += SALE_PRICE;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d sells player %d a secret of player %d for %d BC: %s",
                (int)MOX::stardate, (int)sold.holder, (int)buyer, (int)sold.owner, (int)SALE_PRICE, Kind_Name_(sold.kind));
        } else {
            intrigue::Owe(buyer, sold.holder);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d sells player %d a secret of player %d for a favour: %s",
                (int)MOX::stardate, (int)sold.holder, (int)buyer, (int)sold.owner, Kind_Name_(sold.kind));
        }
        opinion::Add(buyer, sold.owner, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
        counterspy::On_Secret_Leaked(sold.owner);  // the owner suspects a mole, sets a canary trap (off by default)
        Slots()[index] = s_secret{};
        return true;
    }

    bool Smear(int16_t attacker, int16_t target) {
        return Smear_With_Dice(attacker, target, nullptr);
    }

    bool Smear_With_Dice(int16_t attacker, int16_t target, const int32_t* dice_values) {
        if (!smear || !espionage::network || !Alive_(attacker) || !Alive_(target) || attacker == target) return false;
        const int32_t spies = Spies_(attacker, target);
        if (spies == 0 || !espionage::Spend_Points(attacker, target, smear_cost)) return false;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[attacker], &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(attacker, target)}};
        const int32_t difficulty = spy_ops::Difficulty(attacker, spy_ops::OP_FALSE_FLAG, target);
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d smears player %d: %s",
            (int)MOX::stardate, (int)attacker, (int)target, dice::Describe_Check("smears", check, modifiers, 3).c_str());
        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == attacker || o == target || !Computer_(o) || MOX::player[o].contact[target] == 0) continue;
                opinion::Add(o, target, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
            }
            return true;
        }
        if (check.outcome == dice::OUTCOME_EXPOSED || check.outcome == dice::OUTCOME_CATASTROPHE) {
            BILL::Set_Their_Spy_Number(attacker, target, static_cast<uint8_t>(spies - 1));
            opinion::Add(target, attacker, opinion::REASON_SMEARED_US, SMEARED_OPINION);
            espionage::On_Spy_Caught(target, attacker);
        }
        return false;
    }

    void Turn() {
        if (enabled) {
            Learn_All_();
            if (MOX::stardate % PERIOD == 9) Use_All_();
        }
        if (smear && MOX::stardate % PERIOD == 1) Smear_All_();
    }

} // namespace secrets
