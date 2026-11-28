#include "pch.h"

#include "game/discoveries.h"

#include "game/choices.h"
#include "game/colcalc.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/haccess.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/ships.h"
#include "game/texts.h"

#include <algorithm>
#include <climits>

// The behaviour of the discoveries (events-catalog.md 3.8); the records and the save section are in
// discoveries_state.cpp. game_random is drawn only with events discoveries on: the chance of a new find, the
// pick of a kind or a place, the dice of a check, the revolt of the natives and the blast of a power plant.
namespace discoveries {

    namespace {
        constexpr int8_t SHIP_STATUS_ORBIT = 0;
        constexpr int8_t SHIP_STATUS_GONE = 5;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        void Log_(int16_t player, int16_t star, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "discoveries: stardate %d, player %d, star %d: %s", (int)MOX::stardate, (int)player, (int)star, what);
        }

        void Add_Bc_(int16_t player, int32_t delta) {
            s_player& me = MOX::player[player];
            me.bc = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.bc) + delta, 0, INT32_MAX / 2));
        }

        void Add_Research_(int16_t player, int32_t points) {
            s_player& me = MOX::player[player];
            me.research_accumulated = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.research_accumulated) + points, 0, INT32_MAX / 2));
        }

        // A technology application if one is left, else research points.
        void Reward_App_(int16_t player, int32_t fallback_points) {
            const int16_t app = projects::Cheapest_Unknown_App(player);
            if (app >= 0) {
                COLCALC::Player_Gets_Tech_App(&MOX::player[player], app);
            } else {
                Add_Research_(player, fallback_points);
            }
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->record[i] = s->record[i + 1];
            s->record[--s->count] = s_record{};
        }

        void Mark_Used_(int16_t star) {
            State()->used[star >> 3] = static_cast<uint8_t>(State()->used[star >> 3] | (1u << (star & 7)));
        }

        bool Colony_Ok_(int16_t colony, int16_t player) {
            return colony >= 0 && colony < MOX::NUM_COLONIES && colony < MAX_COLONIES && MOX::colony[colony].owner == player &&
                   MOX::colony[colony].n_pops > 0;
        }

        int16_t Colony_Star_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            if (planet < 0 || planet >= MAX_PLANETS) return -1;
            const int16_t star = MOX::planet[planet].star_index;
            return star >= 0 && star < MOX::NUM_STARS && star < MAX_STARS ? star : -1;
        }

        bool Monster_At_(int16_t star) {
            for (int16_t i = 0; i < MOX::NUM_SHIPS && i < MAX_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner >= MAX_PLAYERS && ship.status < SHIP_STATUS_GONE && ship.location == star) return true;
            }
            return false;
        }

        // The empire with the most ship points in orbit at the star, -1 when none.
        int16_t Landing_Owner_(int16_t star) {
            int32_t points[MAX_PLAYERS] = {};
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (ship.location != star || ship.status != SHIP_STATUS_ORBIT || !Alive_(ship.owner)) continue;
                points[ship.owner] += ship.d.size + 1;
            }
            int16_t best = -1;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (points[p] > 0 && (best < 0 || points[p] > points[best])) best = p;
            }
            return best;
        }

        // The empires of the finder race find vaults better.
        bool Is_Finder_(int16_t player) {
            return State()->finder_race != NO_RACE && MOX::player[player].race == State()->finder_race;
        }

        // With the first vault of the galaxy one race among those of the living empires is chosen (a draw).
        void Choose_Finder_Race_() {
            uint8_t races[MAX_PLAYERS];
            int32_t n = 0;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p) || MOX::player[p].race >= MAX_RACE) continue;
                bool seen = false;
                for (int32_t i = 0; i < n && !seen; ++i) seen = races[i] == MOX::player[p].race;
                if (!seen) races[n++] = MOX::player[p].race;
            }
            if (n == 0) return;
            State()->finder_race = races[Pick_(n)];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "discoveries: stardate %d, the galaxy: the vaults of the guardians are placed, race %d finds them better",
                (int)MOX::stardate, (int)State()->finder_race);
        }

        void Lose_Pop_(int16_t colony) {
            s_colony& c = MOX::colony[colony];
            if (c.n_pops >= 2) --c.n_pops;
            COLCALC::Colony_Calculation(colony);
        }

        void Blast_(int16_t player, int16_t star, int16_t colony) {
            int16_t pops = 0;
            int16_t buildings = 0;
            EVENTS::Allocate_Earthquake_Hit(colony, &pops, &buildings);
            COLCALC::Colony_Calculation(colony);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "discoveries: stardate %d, player %d, star %d: the power plant blasts, %d population and %d buildings lost",
                (int)MOX::stardate, (int)player, (int)star, (int)pops, (int)buildings);
        }

        // Candidates ---------------------------------------------------------------------------------------

        // Colonies of `player` at stars that suit `kind` and have had no discovery.
        int32_t Colony_Candidates_(e_kind kind, int16_t player, int16_t star, int16_t* out) {
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (!Colony_Ok_(c, player)) continue;
                const int16_t s = Colony_Star_(c);
                if (s < 0 || Used(s) || (star >= 0 && s != star)) continue;
                if (kind == KIND_NATIVES && MOX::star[s].system_special != SYSTEM_SPECIAL_NATIVES) continue;
                if (kind == KIND_TOMB && (MOX::star[s].system_special != SYSTEM_SPECIAL_ANCIENT_ARTIFACTS || projects::Studied(s) || Monster_At_(s))) continue;
                bool seen = false;
                for (int32_t i = 0; i < n && !seen; ++i) seen = Colony_Star_(out[i]) == s;
                if (!seen) out[n++] = c;
            }
            return n;
        }

        s_record New_(e_kind kind, int16_t player, int16_t star, int16_t colony, int32_t turns) {
            s_record r;
            r.kind = kind;
            r.player = static_cast<int8_t>(player);
            r.star = star;
            r.other = colony;
            r.started = MOX::stardate;
            r.ends = MOX::stardate + turns;
            return r;
        }

        // The find starts at colony `c` with way `opt` (costs are paid now).
        bool Begin_Colony_Kind_(e_kind kind, int16_t player, int16_t c, int32_t opt) {
            const int16_t s = Colony_Star_(c);
            int32_t turns = TOMB_DIG_TURNS;
            const char* what = "";
            switch (kind) {
                case KIND_POWER_PLANT:
                    turns = POWER_TURNS;
                    what = opt == OPT_POWER_TEST ? "an ancient power plant is found: it is tested on a dead world"
                           : opt == OPT_POWER_DISASSEMBLE ? "an ancient power plant is found: it is taken apart"
                                                          : "an ancient power plant is found: it is installed on the colony";
                    if (opt == OPT_POWER_TEST) Add_Bc_(player, -POWER_TEST_COST);
                    break;
                case KIND_SYMBIONTS:
                    turns = SYMB_TURNS;
                    what = opt == OPT_SYMB_STUDY ? "sleeping symbionts are found: their ship is studied"
                           : opt == OPT_SYMB_WAKE ? "sleeping symbionts are found: they are woken"
                                                  : "sleeping symbionts are found: a volunteer bonds with them";
                    break;
                case KIND_NATIVES:
                    turns = NATIVES_TURNS;
                    what = opt == OPT_NAT_LEAVE ? "natives on the continent: they are left alone"
                           : opt == OPT_NAT_TEACH ? "natives on the continent: they are taught"
                                                  : "natives on the continent: they are put to work";
                    if (opt == OPT_NAT_TEACH) Add_Bc_(player, -NATIVES_TEACH_COST);
                    if (opt == OPT_NAT_WORK) Add_Bc_(player, NATIVES_WORK_BC);
                    break;
                default: what = "a tomb with guardians is found: the dig begins"; break;
            }
            s_record r = New_(kind, player, s, c, turns);
            r.option = static_cast<int8_t>(opt);
            State()->record[State()->count++] = r;
            Mark_Used_(s);
            Log_(player, s, what);
            return true;
        }

        // `ask`: a human with the setting on; the colony is picked as always and the find is queued, nothing is started.
        bool Start_Colony_Kind_(e_kind kind, int16_t player, int16_t star, int32_t option, bool ask) {
            int16_t cand[MAX_COLONIES];
            const int32_t n = Colony_Candidates_(kind, player, star, cand);
            if (n == 0) return false;
            const int16_t c = cand[Pick_(n)];
            const bool choice = kind != KIND_TOMB;
            if (choice && ask) {
                choices::s_choice q;
                q.module = choices::MODULE_DISCOVERIES;
                q.kind = kind;
                q.player = static_cast<int8_t>(player);
                q.target = c;
                q.other = Colony_Star_(c);
                q.asked = MOX::stardate;
                if (choices::Add(q)) return true;
            }  // the queue is full: the first option, as without the setting
            const int32_t opt = !choice ? 0 : (option >= 0 ? std::clamp<int32_t>(option, 0, 2) : Choose(kind, player));
            return Begin_Colony_Kind_(kind, player, c, opt);
        }

        bool Start_Vault_(int16_t player, int16_t star) {
            s_state* st = State();
            if (st->vaults_found >= VAULTS_TOTAL) return false;
            int16_t cand[MAX_STARS];
            int32_t n = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (((MOX::star[s].visited >> player) & 1) == 0 || Used(s) || (star >= 0 && s != star)) continue;
                cand[n++] = s;
            }
            if (n == 0) return false;
            if (st->finder_race == NO_RACE) Choose_Finder_Race_();
            const int16_t s = cand[Pick_(n)];
            st->record[st->count++] = New_(KIND_VAULT, player, s, -1, VAULT_TURNS);
            ++st->vaults_found;
            Mark_Used_(s);
            Log_(player, s, "a vault of the guardians is found, the study begins");
            return true;
        }

        // Checks and endings --------------------------------------------------------------------------------

        bool Has_Check_(const s_record& r) {
            return r.kind == KIND_VAULT || r.kind == KIND_SYMBIONTS || (r.kind == KIND_POWER_PLANT && r.option == OPT_POWER_DISASSEMBLE);
        }

        int32_t Difficulty_(const s_record& r) {
            if (r.kind == KIND_VAULT) return VAULT_DIFFICULTY;
            if (r.kind == KIND_POWER_PLANT) return POWER_DIFFICULTY;
            return r.option == OPT_SYMB_STUDY ? SYMB_STUDY_DIFFICULTY : r.option == OPT_SYMB_WAKE ? SYMB_WAKE_DIFFICULTY : SYMB_BOND_DIFFICULTY;
        }

        void Apply_Check_(const s_record& r, dice::e_outcome outcome) {
            const int16_t p = r.player;
            const bool good = outcome >= dice::OUTCOME_SUCCESS;
            const bool brilliant = outcome >= dice::OUTCOME_BRILLIANT;
            const bool bad = outcome <= dice::OUTCOME_EXPOSED;
            if (r.kind == KIND_VAULT) {
                if (good) {
                    Reward_App_(p, APP_FALLBACK_RESEARCH);
                    Add_Bc_(p, VAULT_BC);
                    if (brilliant) Add_Research_(p, VAULT_BRILLIANT_RESEARCH);
                    Log_(p, r.star, "the vault opens: knowledge and treasure");
                } else {
                    Log_(p, r.star, "the vault stays shut and is lost");
                }
            } else if (r.kind == KIND_POWER_PLANT) {
                if (good) {
                    Reward_App_(p, APP_FALLBACK_RESEARCH);
                    if (brilliant) Add_Bc_(p, POWER_BRILLIANT_BC);
                    Log_(p, r.star, "the power plant is understood: a technology gained");
                } else if (bad) {
                    Blast_(p, r.star, r.other);
                } else {
                    Log_(p, r.star, "the power plant yields nothing");
                }
            } else if (r.option == OPT_SYMB_STUDY) {
                if (good) {
                    Add_Research_(p, brilliant ? 2 * SYMB_STUDY_RESEARCH : SYMB_STUDY_RESEARCH);
                    Log_(p, r.star, "the study of the symbionts' ship pays off");
                } else {
                    Log_(p, r.star, "the study of the symbionts' ship finds nothing");
                }
            } else if (r.option == OPT_SYMB_WAKE) {
                if (good) {
                    Reward_App_(p, APP_FALLBACK_RESEARCH);
                    if (brilliant) Add_Research_(p, SYMB_BRILLIANT_RESEARCH);
                    Log_(p, r.star, "the awakened symbionts share a technology");
                } else if (bad) {
                    if (!Monster_At_(r.star) && SHIPS::Build_Nonplayer_Ship(SYMB_GUARDIAN_TYPE, r.star, 0) >= 0) {
                        Log_(p, r.star, "the symbionts wake hostile");
                    } else {
                        Log_(p, r.star, "the symbionts wake hostile and flee");
                    }
                } else {
                    Log_(p, r.star, "the symbionts sleep on");
                }
            } else {
                if (good) {
                    Add_Research_(p, brilliant ? SYMB_BOND_RESEARCH * 3 / 2 : SYMB_BOND_RESEARCH);
                    Log_(p, r.star, "the volunteer bonds with the symbionts: great insight");
                } else if (bad) {
                    Lose_Pop_(r.other);
                    Log_(p, r.star, "the bond fails, the volunteer is lost");
                } else {
                    Log_(p, r.star, "the bond brings nothing");
                }
            }
        }

        // The end of a record without a check, or the wake of a tomb. True when the record is gone.
        bool Finish_(s_record& r) {
            const int16_t p = r.player;
            switch (r.kind) {
                case KIND_POWER_PLANT:
                    if (r.option == OPT_POWER_TEST) {
                        Add_Research_(p, POWER_TEST_RESEARCH);
                        Log_(p, r.star, "the test of the power plant gives data");
                    } else {  // install
                        s_colony& c = MOX::colony[r.other];
                        if (c.buildings[BUILDING_AUTOMATED_FACTORY] == 0) {
                            c.buildings[BUILDING_AUTOMATED_FACTORY] = 1;
                        } else if (c.buildings[BUILDING_ROBOTIC_FACTORY] == 0) {
                            c.buildings[BUILDING_ROBOTIC_FACTORY] = 1;
                        } else {
                            Add_Bc_(p, POWER_FALLBACK_BC);
                        }
                        if (c.n_pops >= 3) --c.n_pops;
                        COLCALC::Colony_Calculation(r.other);
                        Log_(p, r.star, "the power plant runs the colony's industry, the people thin out");
                    }
                    return true;
                case KIND_NATIVES:
                    if (r.option == OPT_NAT_LEAVE) {
                        Add_Bc_(p, NATIVES_GIFT_BC);
                        Log_(p, r.star, "the natives, left in peace, send a gift");
                    } else if (r.option == OPT_NAT_TEACH) {
                        s_colony& c = MOX::colony[r.other];
                        if (c.buildings[BUILDING_RESEARCH_LAB] == 0) {
                            c.buildings[BUILDING_RESEARCH_LAB] = 1;
                            COLCALC::Colony_Calculation(r.other);
                        } else {
                            Add_Research_(p, NATIVES_TEACH_RESEARCH);
                        }
                        Log_(p, r.star, "the taught natives build a laboratory");
                    } else if (static_cast<int32_t>(game_random::Random(100)) <= NATIVES_REVOLT_PERCENT) {
                        Add_Bc_(p, -NATIVES_REVOLT_BC);
                        Lose_Pop_(r.other);
                        Log_(p, r.star, "the native workers revolt");
                    } else {
                        Log_(p, r.star, "the native workers stay quiet");
                    }
                    return true;
                case KIND_TOMB:
                    if (!Monster_At_(r.star) && SHIPS::Build_Nonplayer_Ship(TOMB_GUARDIAN_TYPE, r.star, 0) >= 0) {
                        r.stage = 1;
                        r.ends = MOX::stardate + TOMB_GUARD_TURNS;
                        Log_(p, r.star, "the dig wakes a guardian of the tomb");
                        return false;
                    }
                    Log_(p, r.star, "the tomb is sealed again");
                    return true;
                default: return true;
            }
        }

        // One turn of the record at `index`; true when it is gone.
        bool Step_(int32_t index) {
            s_record& r = State()->record[index];
            if (!Alive_(r.player)) {
                Log_(r.player, r.star, "the find is dropped, the empire is gone");
                Remove_Record_(index);
                return true;
            }
            const bool needs_colony = r.kind != KIND_VAULT && !(r.kind == KIND_TOMB && r.stage == 1);
            if (needs_colony && !Colony_Ok_(r.other, r.player)) {
                Log_(r.player, r.star, "the find is lost with the colony");
                Remove_Record_(index);
                return true;
            }
            if (r.kind == KIND_TOMB && r.stage == 1) {
                if (!Monster_At_(r.star)) {
                    const int16_t lander = Landing_Owner_(r.star);
                    if (lander >= 0) {
                        Reward_App_(lander, TOMB_FALLBACK_RESEARCH);
                        Add_Bc_(lander, TOMB_BC);
                        Log_(lander, r.star, "the guardian of the tomb is beaten: the treasure is taken");
                    } else {
                        Log_(r.player, r.star, "the guardian of the tomb is gone, nobody claims the treasure");
                    }
                    Remove_Record_(index);
                    return true;
                }
                if (MOX::stardate >= r.ends) {
                    Log_(r.player, r.star, "the tomb is given up, the guardian stays");
                    Remove_Record_(index);
                    return true;
                }
                return false;
            }
            if (MOX::stardate < r.ends) return false;
            if (Has_Check_(r)) {
                Check_With_Dice(index, nullptr);
                return true;
            }
            if (Finish_(r)) {
                Remove_Record_(index);
                return true;
            }
            return false;
        }

        void Spawn_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p) || static_cast<int32_t>(game_random::Random(1000)) > DISCOVERY_CHANCE) continue;
                if (Active(p) >= MAX_PER_EMPIRE) continue;
                const int32_t first = Pick_(KIND_LAST);
                for (int32_t j = 0; j < KIND_LAST; ++j) {
                    if (Start(static_cast<e_kind>(1 + (first + j) % KIND_LAST), p)) return;  // one new find a turn
                }
            }
        }
    } // namespace

    int32_t Choose(e_kind kind, int16_t player) {
        if (!enabled || !Computer_(player)) return 0;
        const int32_t personality = MOX::player[player].personality;
        const bool gentle = personality == PLAYER_PERSONALITY_HONORABLE || personality == PLAYER_PERSONALITY_PACIFIST;
        const bool bold = personality == PLAYER_PERSONALITY_RUTHLESS || personality == PLAYER_PERSONALITY_AGGRESSIVE;
        switch (kind) {
            case KIND_POWER_PLANT: return gentle ? OPT_POWER_TEST : bold ? OPT_POWER_INSTALL : OPT_POWER_DISASSEMBLE;
            case KIND_SYMBIONTS:
                if (gentle || personality == PLAYER_PERSONALITY_XENOPHOBIC) return OPT_SYMB_STUDY;
                return bold ? OPT_SYMB_WAKE : OPT_SYMB_BOND;
            case KIND_NATIVES:
                if (personality == PLAYER_PERSONALITY_PACIFIST) return OPT_NAT_LEAVE;
                if (personality == PLAYER_PERSONALITY_HONORABLE) return OPT_NAT_TEACH;
                return bold || personality == PLAYER_PERSONALITY_XENOPHOBIC ? OPT_NAT_WORK : OPT_NAT_TEACH;
            default: return 0;
        }
    }

    bool Start(e_kind kind, int16_t player, int16_t star, int32_t option) {
        if (!enabled || !Alive_(player) || State()->count >= MAX_RECORDS) return false;
        switch (kind) {
            case KIND_POWER_PLANT:
            case KIND_SYMBIONTS:
            case KIND_NATIVES:
            case KIND_TOMB: return Start_Colony_Kind_(kind, player, star, option, option < 0 && choices::Ask_Human(player));
            case KIND_VAULT: return Start_Vault_(player, star);
            default: return false;
        }
    }

    namespace {
        // The colony of a pending choice can still take the find: the empire lives, there is room for a record and the
        // colony is among the candidates as at the start.
        bool Choice_Valid_(const choices::s_choice& c) {
            if (!enabled || c.kind < KIND_POWER_PLANT || c.kind > KIND_NATIVES || !Alive_(c.player)) return false;
            if (State()->count >= MAX_RECORDS || !Colony_Ok_(c.target, c.player)) return false;
            const int16_t star = Colony_Star_(c.target);
            if (star < 0) return false;
            int16_t cand[MAX_COLONIES];
            const int32_t n = Colony_Candidates_(static_cast<e_kind>(c.kind), c.player, star, cand);
            for (int32_t i = 0; i < n; ++i) {
                if (cand[i] == c.target) return true;
            }
            return false;
        }
    } // namespace

    bool Choice_Text(const choices::s_choice& c, choices::s_text* t) {
        if (!Choice_Valid_(c)) return false;
        *t = choices::s_text{};
        const e_kind kind = static_cast<e_kind>(c.kind);
        char place[64] = {};
        const int16_t planet = MOX::colony[c.target].planet;
        if (planet >= 0 && planet < MAX_PLANETS) {
            char name[64];
            HACCESS::Do_Get_Planet_Name(planet, name, 0);
            snprintf(place, sizeof(place), "%s", name);
        } else {
            snprintf(place, sizeof(place), "%s", texts::Get("choices.common.a_colony"));
        }
        const int32_t bc = MOX::player[c.player].bc;
        auto N = [](int32_t v) { return std::to_string(v); };
        const char* part = kind == KIND_POWER_PLANT ? "power_plant" : kind == KIND_SYMBIONTS ? "symbionts" : "natives";
        const std::string prefix = std::string("choices.discoveries.") + part + ".";
        texts::Format(t->title, sizeof(t->title), (prefix + "title").c_str(), {});
        texts::Format(t->body, sizeof(t->body), (prefix + "body").c_str(), {{"place", std::string(place)}});
        auto Option = [&](const char* name, std::initializer_list<std::pair<const char*, std::string>> args) {
            texts::Format(t->option[t->count], sizeof(t->option[t->count]), (prefix + name).c_str(), args);
            ++t->count;
        };
        switch (kind) {
            case KIND_POWER_PLANT:
                // A poor empire pays what it has (Add_Bc_ stops at zero): the line shows the real sum.
                Option("option1", {{"cost", N(std::min(POWER_TEST_COST, bc))}, {"research", N(POWER_TEST_RESEARCH)}, {"turns", N(POWER_TURNS)}});
                Option("option2", {{"turns", N(POWER_TURNS)}});
                Option("option3", {{"turns", N(POWER_TURNS)}});
                break;
            case KIND_SYMBIONTS:
                Option("option1", {{"research", N(SYMB_STUDY_RESEARCH)}, {"turns", N(SYMB_TURNS)}});
                Option("option2", {{"turns", N(SYMB_TURNS)}});
                Option("option3", {{"research", N(SYMB_BOND_RESEARCH)}, {"turns", N(SYMB_TURNS)}});
                break;
            default:
                Option("option1", {{"gift", N(NATIVES_GIFT_BC)}, {"turns", N(NATIVES_TURNS)}});
                Option("option2", {{"cost", N(std::min(NATIVES_TEACH_COST, bc))}, {"turns", N(NATIVES_TURNS)}});
                Option("option3", {{"bc", N(NATIVES_WORK_BC)}, {"percent", N(NATIVES_REVOLT_PERCENT)}, {"loss", N(NATIVES_REVOLT_BC)},
                                      {"turns", N(NATIVES_TURNS)}});
                break;
        }
        return true;
    }

    void Choice_Apply(const choices::s_choice& c, int32_t option) {
        if (!Choice_Valid_(c)) return;
        Begin_Colony_Kind_(static_cast<e_kind>(c.kind), c.player, c.target, std::clamp<int32_t>(option, 0, 2));
    }

    void Check_With_Dice(int32_t index, const int32_t* dice_values) {
        s_state* s = State();
        if (index < 0 || index >= s->count || !Has_Check_(s->record[index])) return;
        const s_record r = s->record[index];
        dice::s_modifier modifiers[2];
        int32_t count = 0;
        modifiers[count++] = {"skill", projects::Skill(r.player)};
        if (r.kind == KIND_VAULT && Is_Finder_(r.player)) modifiers[count++] = {"race finds vaults better", VAULT_FINDER_BONUS};
        const dice::e_roll_kind kind = static_cast<dice::e_roll_kind>(dice::roll_kind);
        const int32_t difficulty = Difficulty_(r);
        const dice::s_check check = dice_values != nullptr ? dice::Resolve_Check(kind, dice_values, modifiers, count, difficulty)
                                                           : dice::Roll_Check(modifiers, count, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "discoveries: stardate %d, player %d, star %d, %s", (int)MOX::stardate, (int)r.player, (int)r.star,
            dice::Describe_Check("study the find", check, modifiers, count).c_str());
        Remove_Record_(index);
        Apply_Check_(r, check.outcome);
    }

    void Run_Records() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            if (!Step_(i)) ++i;
        }
    }

    void Turn() {
        if (!enabled) return;
        Run_Records();
        Spawn_();
    }

} // namespace discoveries
