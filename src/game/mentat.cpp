#include "pch.h"

#include "game/mentat.h"
#include "game/texts.h"

#include "game/choices.h"
#include "game/council.h"
#include "game/crises.h"
#include "game/debug_overlay.h"
#include "game/espionage.h"
#include "game/gendraw.h"
#include "game/hostages.h"
#include "game/msg.h"
#include "game/news.h"
#include "game/opinion.h"
#include "game/shipstak.h"
#include "game/stability.h"
#include "game/text_language.h"
#include "game/ultimatums.h"

// The mentat's content (docs/LocalLLM/mentat.md, M1). Read-only: see the list of the engine calls in mentat.h. No draw
// from game_random, no state of the game changed, nothing saved.
namespace mentat {

    namespace {
        constexpr int32_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;  // as news.cpp
        constexpr int32_t ANTARAN_OWNER = 8;                 // the Antarans' ships (report.cpp, combfind.cpp)
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int32_t DIPLOMACY_WAR = 0;                 // msg.cpp: the codes of MSG_TYPE_DIPLOMACY
        constexpr int32_t DIPLOMACY_PEACE = 1;
        constexpr int32_t DIPLOMACY_ALLIANCE = 2;
        constexpr int32_t DIPLOMACY_NONAGGRESSION = 3;
        constexpr int32_t DIPLOMACY_PEACE_ENDED = 4;
        constexpr uint32_t POP_REBEL_FLAG = 0x400;           // invasion.cpp Check_Rebellion
        constexpr int32_t RATINGS = news::RATING_COUNT;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int32_t p) {
            return p >= 0 && p < Players_() && MOX::player[p].eliminated == 0;
        }

        bool At_War_(int16_t player, int32_t other) {
            return Alive_(other) && other != player && MOX::player[player].contact[other] != 0 &&
                MOX::player[player].treaty[other] >= TREATY_FIRST_WAR;
        }

        std::string Num_(int64_t value) {
            return std::to_string(value);
        }

        // A name of the game, in the game's bytes (converted when the panel is drawn).
        std::string Name_(int32_t p) {
            if (p == ANTARAN_OWNER) return texts::Get("mentat.antarans");
            if (p < 0 || p >= Players_()) return "?";
            const s_player& player = MOX::player[p];
            return std::string(player.race_name, strnlen(player.race_name, sizeof(player.race_name)));
        }

        std::string Star_Name_(int32_t star) {
            if (star < 0 || star >= MAX_STARS) return "?";
            const s_star_data& data = MOX::star[star];
            return std::string(data.name, strnlen(data.name, sizeof(data.name)));
        }

        s_text Text_(const char* key, texts::t_args args = {}) {
            return s_text{key, std::move(args)};
        }

        // A warship's weight: 1 for the hull and the damage its weapons can do (as news.cpp counts them).
        int32_t Ship_Strength_(const s_ship_data& ship) {
            if (ship.d.ship_type != SHIP_TYPE_COMBAT) return 0;
            int32_t strength = 1;
            for (const s_ship_weapons& mount : ship.d.ship_weapon) {
                if (mount.type <= WEAPON_NO_WEAPONS || mount.type >= WEAPON_COUNT || mount.count <= 0) continue;
                strength += mount.count * TECHDATA::weapons[mount.type].max_damage;
            }
            return strength;
        }

        bool Ship_Alive_(const s_ship_data& ship) {
            return ship.owner >= 0 && ship.owner <= MAX_PLAYERS && ship.status < SHIP_STATUS_FIRST_NOT_ALIVE;
        }

        // The star of a colony of `player`, -1 when the colony is not its own or has no star.
        int32_t Colony_Star_(int32_t colony, int16_t player) {
            if (colony < 0 || colony >= MOX::NUM_COLONIES) return -1;
            const s_colony& c = MOX::colony[colony];
            if (c.owner != player || c.planet < 0 || c.planet >= MAX_PLANETS) return -1;
            const int32_t star = MOX::planet[c.planet].star_index;
            return star >= 0 && star < MAX_STARS ? star : -1;
        }

        // The distinct stars where `player` has a colony.
        std::vector<int32_t> Colony_Stars_(int16_t player) {
            std::vector<int32_t> stars;
            for (int32_t i = 0; i < MOX::NUM_COLONIES; ++i) {
                const int32_t star = Colony_Star_(i, player);
                if (star >= 0 && std::find(stars.begin(), stars.end(), star) == stars.end()) stars.push_back(star);
            }
            return stars;
        }

        bool Near_(const s_ship_data& ship, int32_t star) {
            const int32_t dx = ship.x - MOX::star[star].x;
            const int32_t dy = ship.y - MOX::star[star].y;
            return dx * dx + dy * dy <= NEAR_UNITS * NEAR_UNITS;
        }

        // The turn's messages of the game for `player`: the queue the game shows at the start of the turn.
        std::vector<const s_MSG*> Messages_(int16_t player) {
            std::vector<const s_MSG*> out;
            if (player < 0 || player >= MAX_PLAYERS) return out;
            const int32_t count = MSG_::N_Msgs_For_Player(player);
            const int32_t first = MSG_::First_Msg_For_Player(player);
            for (int32_t i = 0; i < count && first + i < MAX_MESSAGES; ++i) out.push_back(&MSG_::msgs[first + i]);
            return out;
        }

        // ---- the critical situations ----

        // Wars that began with `player` this turn (the game's own diplomacy messages), the other empires in order.
        std::vector<int32_t> New_Wars_(int16_t player) {
            std::vector<int32_t> others;
            for (const s_MSG* msg : Messages_(player)) {
                if (msg->msg_type != MSG_TYPE_DIPLOMACY || msg->u.diplomacy.diplomacy_idx != DIPLOMACY_WAR) continue;
                const int32_t a = msg->u.diplomacy.plr_a;
                const int32_t b = msg->u.diplomacy.plr_b;
                const int32_t other = a == player ? b : (b == player ? a : -1);
                if (other < 0 || !At_War_(player, other)) continue;
                if (std::find(others.begin(), others.end(), other) == others.end()) others.push_back(other);
            }
            return others;
        }

        bool Check_War_(int16_t player, s_alert* out) {
            const std::vector<int32_t> wars = New_Wars_(player);
            if (wars.empty()) return false;
            const int32_t other = wars[0];
            const s_player& me = MOX::player[player];
            out->text = wars.size() == 1 ? Text_("mentat.alert.war", {{"name", Name_(other)}})
                                         : Text_("mentat.alert.wars", {{"name", Name_(other)}, {"count", Num_(wars.size() - 1)}});
            out->why = Text_("mentat.why.war", {{"name", Name_(other)}, {"relations", Num_(me.relations[other])},
                                                   {"ships", Num_(me.ship_war_losses[other])}, {"colonies", Num_(me.colony_war_losses[other])}});
            return true;
        }

        bool Check_Fleet_(int16_t player, s_alert* out) {
            if (MOX::ship == nullptr) return false;
            int32_t best_star = -1;
            int32_t best_enemy = 0;
            int32_t best_mine = 0;
            int32_t best_gap = 0;
            int32_t best_owner = -1;
            for (const int32_t star : Colony_Stars_(player)) {
                int32_t mine = 0;
                int32_t enemy = 0;
                int32_t owner = -1;
                int32_t owner_strength = 0;
                for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                    const s_ship_data& ship = MOX::ship[i];
                    if (!Ship_Alive_(ship)) continue;
                    const int32_t strength = Ship_Strength_(ship);
                    if (strength == 0) continue;
                    if (ship.owner == player) {
                        if (ship.status == 0 && ship.location == star) mine += strength;
                    } else if (At_War_(player, ship.owner) && Near_(ship, star) &&
                               SHIPSTAK::Is_Ship_Detected_By(static_cast<int16_t>(i), static_cast<uint8_t>(player))) {
                        enemy += strength;
                        if (strength > owner_strength) {
                            owner_strength = strength;
                            owner = ship.owner;
                        }
                    }
                }
                if (enemy > mine && enemy - mine > best_gap) {
                    best_gap = enemy - mine;
                    best_star = star;
                    best_enemy = enemy;
                    best_mine = mine;
                    best_owner = owner;
                }
            }
            if (best_star < 0) return false;
            out->text = Text_("mentat.alert.fleet", {{"star", Star_Name_(best_star)}, {"name", Name_(best_owner)}});
            out->why = Text_("mentat.why.fleet", {{"star", Star_Name_(best_star)}, {"theirs", Num_(best_enemy)}, {"mine", Num_(best_mine)}});
            return true;
        }

        // The council's session within COUNCIL_WARNING_TURNS and the expected votes: each member votes for a nominee when
        // its score draws it (Random(200) <= score, council.cpp Vote_Check) and for none when it supports both or neither;
        // the human's own vote is not counted for anyone. Expected votes in whole votes.
        struct s_council_view {
            uint8_t nominee[2] = {};
            int32_t expected[2] = {};
            int32_t total = 0;
            int32_t threshold = 0;
            int32_t my_votes = 0;
        };

        bool Council_View_(int16_t player, int32_t within, s_council_view* view) {
            if (!council::Session_Due_Within(within)) return false;
            if (!council::Predict_Nominees(view->nominee)) return false;
            int64_t weighted[2] = {0, 0};  // votes x per mille x per mille
            for (int16_t m = 0; m < Players_(); ++m) {
                if (!Alive_(m)) continue;
                const int32_t votes = council::Get_Num_Votes(static_cast<uint8_t>(m));
                view->total += votes;
                if (m == player) view->my_votes = votes;
                if (m == view->nominee[0]) {
                    weighted[0] += static_cast<int64_t>(votes) * 1000000;
                } else if (m == view->nominee[1]) {
                    weighted[1] += static_cast<int64_t>(votes) * 1000000;
                } else if (m != player) {
                    const int32_t p0 = std::clamp<int32_t>(council::Vote_Score(view->nominee[0], m, view->nominee[1]), 0, 200) * 5;
                    const int32_t p1 = std::clamp<int32_t>(council::Vote_Score(view->nominee[1], m, view->nominee[0]), 0, 200) * 5;
                    weighted[0] += static_cast<int64_t>(votes) * p0 * (1000 - p1);
                    weighted[1] += static_cast<int64_t>(votes) * p1 * (1000 - p0);
                }
            }
            for (int i = 0; i < 2; ++i) view->expected[i] = static_cast<int32_t>((weighted[i] + 500000) / 1000000);
            view->threshold = (view->total * 2 + 2) / 3;
            return true;
        }

        // The turns until the next session (1..COUNCIL_LOOKAHEAD), 0 when none is that near.
        int32_t Session_In_(int32_t limit) {
            for (int32_t n = 1; n <= limit; ++n) {
                if (council::Session_Due_Within(n)) return n;
            }
            return 0;
        }

        bool Check_Council_Lose_(int16_t player, s_alert* out) {
            s_council_view view;
            if (!Council_View_(player, COUNCIL_WARNING_TURNS, &view)) return false;
            int rival = -1;
            for (int i = 0; i < 2; ++i) {
                if (view.nominee[i] == player || view.expected[i] < view.threshold) continue;
                // A rival that wins: the one you are against in the vote, or one at war with you.
                const bool me_nominee = view.nominee[1 - i] == player;
                if (me_nominee || At_War_(player, view.nominee[i])) rival = view.nominee[i];
            }
            if (rival < 0) return false;
            const int index = view.nominee[0] == rival ? 0 : 1;
            const int32_t turns = Session_In_(COUNCIL_WARNING_TURNS);
            out->text = Text_("mentat.alert.council_lose", {{"name", Name_(rival)}, {"turns", Num_(turns)}});
            out->why = Text_("mentat.why.council_lose", {{"name", Name_(rival)}, {"votes", Num_(view.expected[index])},
                                                            {"needed", Num_(view.threshold)}, {"total", Num_(view.total)}});
            return true;
        }

        bool Check_Council_Short_(int16_t player, s_alert* out) {
            s_council_view view;
            if (!Council_View_(player, COUNCIL_WARNING_TURNS, &view)) return false;
            int me = -1;
            for (int i = 0; i < 2; ++i) {
                if (view.nominee[i] == player) me = i;
            }
            if (me < 0) return false;
            const int32_t gap = view.threshold - view.expected[me];
            const int32_t few = std::max(COUNCIL_FEW_VOTES, view.total / 10);
            if (gap <= 0 || gap > few) return false;
            const int32_t turns = Session_In_(COUNCIL_WARNING_TURNS);
            out->text = Text_("mentat.alert.council_short", {{"gap", Num_(gap)}, {"turns", Num_(turns)}});
            out->why = Text_("mentat.why.council_short", {{"votes", Num_(view.expected[me])}, {"needed", Num_(view.threshold)},
                                                             {"total", Num_(view.total)}});
            return true;
        }

        bool Check_Unrest_(int16_t player, s_alert* out) {
            int32_t worst = 0;
            s_text text;
            s_text why;
            for (int32_t i = 0; i < MOX::NUM_COLONIES; ++i) {
                const int32_t star = Colony_Star_(i, player);
                if (star < 0) continue;
                const s_colony& colony = MOX::colony[i];
                if (colony.outpost_flag != 0) continue;
                int32_t rebels = 0;
                for (int32_t k = 0; k < colony.n_pops && k < MAX_COLONY_POPULATION; ++k) {
                    const uint32_t flags = colony.pop[k];
                    const uint32_t race = flags & 0xF;
                    if ((flags & POP_REBEL_FLAG) != 0 && race < MAX_PLAYERS && !MOX::player[race].eliminated) ++rebels;
                }
                int32_t severity = rebels * 10;
                const int32_t stability_now = stability::Of(static_cast<int16_t>(i));
                const uint8_t* unrest_slot = stability::Unrest_Slot(static_cast<int16_t>(i));
                const int32_t unrest = unrest_slot != nullptr ? *unrest_slot : 0;
                const bool unstable = stability::enabled && stability_now <= stability::rebellion_threshold;
                if (unstable) severity += stability::rebellion_threshold - stability_now + 1 + unrest * 5;
                if (severity <= worst) continue;
                worst = severity;
                if (unstable && unrest >= stability::UNREST_TURNS) {
                    text = Text_("mentat.alert.secession", {{"star", Star_Name_(star)}});
                } else if (unstable) {
                    text = Text_("mentat.alert.unrest", {{"star", Star_Name_(star)}});
                } else {
                    text = Text_("mentat.alert.rebels", {{"star", Star_Name_(star)}, {"count", Num_(rebels)}});
                }
                why = Text_("mentat.why.unrest", {{"star", Star_Name_(star)}, {"rebels", Num_(rebels)}, {"stability", Num_(stability_now)},
                                                     {"turns", Num_(unrest)}, {"limit", Num_(stability::UNREST_TURNS)}});
            }
            if (worst == 0) return false;
            out->text = text;
            out->why = why;
            return true;
        }

        bool Check_Spy_(int16_t player, s_alert* out) {
            int32_t count = 0;
            int32_t where = -1;
            for (const s_MSG* msg : Messages_(player)) {
                if (msg->msg_type != MSG_TYPE_LOST_ATTACKING_SPY) continue;
                if (count == 0) where = msg->u.lost_attacking_spy.player_idx;
                ++count;
            }
            if (count == 0) return false;
            out->text = Text_("mentat.alert.spy", {{"name", Name_(where)}, {"count", Num_(count)}});
            out->why = Text_("mentat.why.spy", {{"name", Name_(where)},
                                                   {"points", Num_(where >= 0 && where < Players_() ? espionage::Points(player, static_cast<int16_t>(where)) : 0)}});
            return true;
        }

        bool Check_Ultimatum_(int16_t player, s_alert* out) {
            const ultimatums::s_state* state = ultimatums::State();
            int32_t best = -1;
            int32_t best_left = 0;
            for (int32_t i = 0; i < state->count && i < ultimatums::MAX_RECORDS; ++i) {
                const ultimatums::s_record& record = state->record[i];
                if (record.victim != player || record.state != ultimatums::STATE_OPEN) continue;
                const int32_t left = std::max(0, record.deadline - MOX::stardate);
                if (left > DEADLINE_TURNS || (best >= 0 && left >= best_left)) continue;
                best = i;
                best_left = left;
            }
            if (best < 0) return false;
            const ultimatums::s_record& record = state->record[best];
            out->text = Text_("mentat.alert.ultimatum", {{"name", Name_(record.demander)}, {"turns", Num_(best_left)}});
            out->why = Text_("mentat.why.ultimatum", {{"name", Name_(record.demander)}, {"amount", Num_(record.amount)},
                                                         {"turns", Num_(best_left)}});
            return true;
        }

        bool Check_Prisoner_(int16_t player, s_alert* out) {
            const hostages::s_state* state = hostages::State();
            int32_t best = -1;
            int32_t best_left = 0;
            for (int32_t i = 0; i < state->count && i < hostages::MAX_HELD; ++i) {
                const hostages::s_held& held = state->held[i];
                if (held.owner != player || held.holder == player || held.kind != hostages::KIND_PRISONER) continue;
                const int32_t left = std::max(0, held.since + hostages::PRISONER_TURNS - MOX::stardate);
                if (left > DEADLINE_TURNS || (best >= 0 && left >= best_left)) continue;
                best = i;
                best_left = left;
            }
            if (best < 0) return false;
            const hostages::s_held& held = state->held[best];
            std::string leader = "?";
            if (MOX::leaders != nullptr && held.leader >= 0 && held.leader < MAX_LEADERS) {
                const s_leader_data& data = MOX::leaders[held.leader];
                leader.assign(data.name, strnlen(data.name, sizeof(data.name)));
            }
            out->text = Text_("mentat.alert.prisoner", {{"leader", leader}, {"name", Name_(held.holder)}, {"turns", Num_(best_left)}});
            out->why = Text_("mentat.why.prisoner", {{"leader", leader}, {"name", Name_(held.holder)}, {"since", Num_(MOX::stardate - held.since)},
                                                        {"term", Num_(hostages::PRISONER_TURNS)}});
            return true;
        }

        bool Check_Bankruptcy_(int16_t player, s_alert* out) {
            const s_player& me = MOX::player[player];
            const int32_t income = me.surplus_bc;
            const int32_t bc = me.bc;
            if (income >= 0 && bc >= 0) return false;
            if (bc >= 0 && bc + BANKRUPTCY_TURNS * income >= 0) return false;
            const int32_t turns = income < 0 ? std::max(0, bc / -income) + 1 : 0;
            out->text = bc < 0 ? Text_("mentat.alert.bankrupt_now", {{"bc", Num_(bc)}})
                               : Text_("mentat.alert.bankrupt", {{"turns", Num_(turns)}});
            out->why = Text_("mentat.why.bankrupt", {{"bc", Num_(bc)}, {"income", Num_(income)}, {"turns", Num_(turns)}});
            return true;
        }

        bool Check_Weariness_(int16_t player, s_alert* out) {
            const crises::s_scale* scale = crises::Find_Scale(crises::SCALE_WAR_WEARINESS, player);
            if (scale == nullptr || scale->value >= crises::SCALE_MAX) return false;
            bool war = false;
            for (int16_t o = 0; o < Players_() && !war; ++o) war = At_War_(player, o);
            if (!war) return false;  // at peace the scale only falls
            const int32_t growth = crises::Weariness_Growth(player);
            if (growth <= 0) return false;
            const int32_t next = (scale->value / crises::SCALE_STAGE + 1) * crises::SCALE_STAGE;
            const int32_t turns = (next - scale->value + growth - 1) / growth;
            if (turns > WEARINESS_TURNS) return false;
            out->text = Text_("mentat.alert.weariness", {{"stage", Num_(next / crises::SCALE_STAGE)}, {"turns", Num_(turns)}});
            out->why = Text_("mentat.why.weariness", {{"value", Num_(scale->value)}, {"max", Num_(crises::SCALE_MAX)}, {"growth", Num_(growth)},
                                                         {"next", Num_(next)}});
            return true;
        }

        bool Check_Crisis_(int16_t player, s_alert* out) {
            const crises::s_active* slots = crises::Slots();
            int32_t best = -1;
            for (int32_t i = 0; i < crises::MAX_ACTIVE; ++i) {
                const crises::s_active& slot = slots[i];
                if (slot.crisis == crises::CRISIS_NONE || slot.player != player || slot.result != crises::RESULT_PENDING || slot.option < 0) continue;
                if (slot.due > MOX::stardate + 1) continue;
                if (best < 0 || slot.due < slots[best].due) best = i;
            }
            if (best < 0) return false;
            const crises::s_active& slot = slots[best];
            const std::string key = "mentat.crisis_name." + Num_(slot.crisis);
            const std::string name = texts::Get(key.c_str());
            out->text = Text_("mentat.alert.crisis", {{"name", name}});
            out->why = Text_("mentat.why.crisis", {{"name", name}, {"option", Num_(slot.option + 1)}, {"delay", Num_(crises::CHECK_DELAY)}});
            return true;
        }

        bool Check_Antarans_(int16_t player, s_alert* out) {
            if (MOX::ship == nullptr) return false;
            int32_t best_star = -1;
            int32_t best_count = 0;
            for (const int32_t star : Colony_Stars_(player)) {
                int32_t count = 0;
                for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                    const s_ship_data& ship = MOX::ship[i];
                    if (!Ship_Alive_(ship) || ship.owner != ANTARAN_OWNER || !Near_(ship, star)) continue;
                    if (SHIPSTAK::Is_Ship_Detected_By(static_cast<int16_t>(i), static_cast<uint8_t>(player))) ++count;
                }
                if (count > best_count) {
                    best_count = count;
                    best_star = star;
                }
            }
            if (best_star < 0) return false;
            out->text = Text_("mentat.alert.antarans", {{"star", Star_Name_(best_star)}});
            out->why = Text_("mentat.why.antarans", {{"star", Star_Name_(best_star)}, {"count", Num_(best_count)}});
            return true;
        }

        // ---- the memory between turns ----

        struct s_memory {
            bool valid = false;
            int32_t stardate = 0;
            int32_t bc = 0;
            int32_t income = 0;
            int8_t relations[MAX_PLAYERS] = {};
            int32_t ratings_stardate = 0;
            int32_t rank[RATINGS] = {};  // 0: none
        };
        s_memory memory[MAX_PLAYERS];
        s_summary current;
        bool open_request = false;
        bool opens_panel = false;
        bool alert_shown = false;
        bool seen = true;

        bool Human_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0 &&
                MOX::player[player].objectives == PLAYER_OBJECTIVE_HUMAN;
        }

        // The place of `player` among itself and the empires it has met in the last ranking (1 = best), 0 when absent.
        int32_t Visible_Rank_(int32_t rating, int16_t player) {
            const news::s_ratings& ratings = news::Ratings();
            int32_t rank = 0;
            for (int32_t k = 0; k < news::RANK_SLOTS; ++k) {
                const int32_t who = ratings.order[rating][k];
                if (who < 0) break;
                if (who != player && (!Alive_(who) || MOX::player[player].contact[who] == 0)) continue;
                ++rank;
                if (who == player) return rank;
            }
            return 0;
        }

        // ---- the summary ----

        std::string Join_(const std::vector<std::string>& parts, const char* separator = ", ") {
            std::string out;
            for (const std::string& part : parts) {
                if (!out.empty()) out += separator;
                out += part;
            }
            return out;
        }

        // The diplomacy of the turn: the game's messages naming an empire pair, and the eliminations the news names.
        void Diplomacy_Events_(int16_t player, std::vector<s_text>* lines) {
            struct s_pair {
                int32_t code;
                int32_t a;
                int32_t b;
            };
            std::vector<s_pair> seen_pairs;
            for (const s_MSG* msg : Messages_(player)) {
                if (msg->msg_type != MSG_TYPE_DIPLOMACY) continue;
                const int32_t code = msg->u.diplomacy.diplomacy_idx;
                const int32_t a = std::min<int32_t>(msg->u.diplomacy.plr_a, msg->u.diplomacy.plr_b);
                const int32_t b = std::max<int32_t>(msg->u.diplomacy.plr_a, msg->u.diplomacy.plr_b);
                bool again = false;
                for (const s_pair& pair : seen_pairs) again = again || (pair.code == code && pair.a == a && pair.b == b);
                if (again || !Alive_(a) || !Alive_(b)) continue;
                seen_pairs.push_back({code, a, b});
                const char* key = nullptr;
                switch (code) {
                    case DIPLOMACY_WAR: key = "mentat.event.war"; break;
                    case DIPLOMACY_PEACE: key = "mentat.event.peace"; break;
                    case DIPLOMACY_ALLIANCE: key = "mentat.event.alliance"; break;
                    case DIPLOMACY_NONAGGRESSION: key = "mentat.event.pact"; break;
                    case DIPLOMACY_PEACE_ENDED: key = "mentat.event.peace_ended"; break;
                    default: break;
                }
                if (key != nullptr) lines->push_back(Text_(key, {{"a", Name_(a)}, {"b", Name_(b)}}));
            }
        }

        // The colonies of the turn: taken (the news the empire may read), founded, lost (the game's own messages).
        void Colony_Counts_(int16_t player, const s_memory& mem, int32_t* taken, int32_t* founded, int32_t* lost) {
            *taken = 0;
            *founded = 0;
            *lost = 0;
            for (const s_MSG* msg : Messages_(player)) {
                if (msg->msg_type == MSG_TYPE_LOST_COLONY) ++*lost;
            }
            const int32_t since = mem.valid ? mem.stardate : MOX::stardate - NEWS_TURNS;
            news::s_item items[news::NEWS_ITEMS];
            const int32_t count = news::Recent_For(player, items, news::NEWS_ITEMS);
            int32_t news_lost = 0;
            for (int32_t i = 0; i < count; ++i) {
                const news::s_item& item = items[i];
                if (item.stardate <= since) continue;
                if (item.kind == news::KIND_COLONY_TAKEN && item.subject == player) ++*taken;
                if (item.kind == news::KIND_COLONY_TAKEN && item.object == player) ++news_lost;
                if (item.kind == news::KIND_COLONY_FOUNDED && item.subject == player) ++*founded;
            }
            *lost = std::max(*lost, news_lost);
        }

        void Eliminations_(int16_t player, const s_memory& mem, std::vector<s_text>* lines) {
            const int32_t since = mem.valid ? mem.stardate : MOX::stardate - NEWS_TURNS;
            news::s_item items[news::NEWS_ITEMS];
            const int32_t count = news::Recent_For(player, items, news::NEWS_ITEMS);
            for (int32_t i = 0; i < count; ++i) {
                if (items[i].kind == news::KIND_ELIMINATED && items[i].stardate > since && items[i].subject != player) {
                    lines->push_back(Text_("mentat.event.eliminated", {{"name", Name_(items[i].subject)}}));
                }
            }
        }

        s_text Wars_Line_(int16_t player) {
            std::vector<std::string> names;
            bool any_contact = false;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Alive_(o) || MOX::player[player].contact[o] == 0) continue;
                any_contact = true;
                if (At_War_(player, o)) names.push_back(Name_(o));
            }
            if (!names.empty()) return Text_("mentat.war.some", {{"names", Join_(names)}});
            return Text_(any_contact ? "mentat.war.none" : "mentat.war.nocontact");
        }

        // Only public facts below the detailed level (the author, 28.11.2026: other empires' reckoning, such as the
        // expected votes, only at the highest level): the session, the two nominees and their votes as the council
        // screen shows them.
        s_text Council_Public_Line_() {
            const int32_t turns = Session_In_(COUNCIL_LOOKAHEAD);
            if (turns == 0) return Text_("");
            uint8_t nominee[2] = {};
            if (!council::Session_Due_Within(COUNCIL_LOOKAHEAD) || !council::Predict_Nominees(nominee)) return Text_("");
            return Text_("mentat.council.public", {{"turns", Num_(turns)}, {"first", Name_(nominee[0])}, {"first_votes", Num_(council::Get_Num_Votes(nominee[0]))},
                                                      {"second", Name_(nominee[1])}, {"second_votes", Num_(council::Get_Num_Votes(nominee[1]))}});
        }

        s_text Council_Line_(int16_t player) {
            const int32_t turns = Session_In_(COUNCIL_LOOKAHEAD);
            if (turns == 0) return Text_("");
            s_council_view view;
            if (!Council_View_(player, COUNCIL_LOOKAHEAD, &view)) return Text_("");
            const bool nominee = view.nominee[0] == player || view.nominee[1] == player;
            const int me = view.nominee[0] == player ? 0 : 1;
            if (nominee) {
                return Text_("mentat.council.nominee", {{"turns", Num_(turns)}, {"votes", Num_(view.expected[me])}, {"needed", Num_(view.threshold)}});
            }
            return Text_("mentat.council.member", {{"turns", Num_(turns)}, {"mine", Num_(view.my_votes)}, {"total", Num_(view.total)},
                                                      {"first", Name_(view.nominee[0])}, {"second", Name_(view.nominee[1])}});
        }

        s_text Ranking_Line_(int16_t player, const s_memory& mem) {
            const news::s_ratings& ratings = news::Ratings();
            if (ratings.stardate == 0) return Text_("");
            const bool fresh = mem.valid ? ratings.stardate > mem.ratings_stardate : MOX::stardate - ratings.stardate <= 1;
            if (!fresh) return Text_("");
            static const char* const keys[RATINGS] = {"mentat.ranking.population", "mentat.ranking.fleet", "mentat.ranking.technology"};  // RATING_POPULATION, FLEET, TECHNOLOGY
            std::vector<std::string> parts;
            for (int32_t r = 0; r < RATINGS; ++r) {
                const int32_t rank = Visible_Rank_(r, player);
                if (rank == 0) continue;
                const int32_t was = mem.valid ? mem.rank[r] : 0;
                parts.push_back(texts::Expand(keys[r], {{"rank", Num_(rank)}, {"was", was > 0 && was != rank ? texts::Expand("mentat.ranking.was", {{"rank", Num_(was)}}, false) : ""}}, false));
            }
            if (parts.empty()) return Text_("");
            return Text_("mentat.ranking_line", {{"turn", Num_(ratings.stardate - 35000)}, {"parts", Join_(parts)}});
        }

        // The brief summary: wars, the turn's diplomacy, colonies, the council, decisions waiting, the ranking.
        void Fill_Brief_(int16_t player, uint8_t level, const s_memory& mem, s_section* section) {
            std::vector<s_text>& lines = section->lines;
            lines.push_back(Wars_Line_(player));
            std::vector<s_text> events;
            Diplomacy_Events_(player, &events);
            Eliminations_(player, mem, &events);
            const size_t shown = std::min<size_t>(events.size(), 2);
            for (size_t i = 0; i < shown; ++i) lines.push_back(events[i]);
            int32_t taken = 0, founded = 0, lost = 0;
            Colony_Counts_(player, mem, &taken, &founded, &lost);
            if (taken + founded + lost > 0) {
                std::vector<std::string> parts;
                if (taken > 0) parts.push_back(texts::Expand("mentat.colonies_part.taken", {{"count", Num_(taken)}}, false));
                if (founded > 0) parts.push_back(texts::Expand("mentat.colonies_part.founded", {{"count", Num_(founded)}}, false));
                if (lost > 0) parts.push_back(texts::Expand("mentat.colonies_part.lost", {{"count", Num_(lost)}}, false));
                lines.push_back(Text_("mentat.colonies_line", {{"parts", Join_(parts)}}));
            }
            const s_text council = level >= DETAIL_DETAILED ? Council_Line_(player) : Council_Public_Line_();
            if (!council.key.empty()) lines.push_back(council);
            const int32_t pending = choices::Count(player);
            if (pending > 0) lines.push_back(Text_("mentat.pending", {{"count", Num_(pending)}}));
            const s_text ranking = Ranking_Line_(player, mem);
            if (!ranking.key.empty()) lines.push_back(ranking);
            if (lines.size() > static_cast<size_t>(MAX_LINES)) lines.resize(MAX_LINES);
        }

        void Fill_Economy_(int16_t player, const s_memory& mem, s_section* section) {
            const s_player& me = MOX::player[player];
            const std::string trend = mem.valid ? texts::Expand("mentat.economy.was", {{"bc", Num_(mem.bc)}, {"income", Num_(mem.income)}}, false) : "";
            section->lines.push_back(Text_("mentat.economy.treasury", {{"bc", Num_(me.bc)}, {"income", Num_(me.surplus_bc)}, {"trend", trend}}));
            if (me.surplus_food < 0) section->lines.push_back(Text_("mentat.economy.famine", {{"food", Num_(me.surplus_food)}}));
        }

        void Fill_Research_(int16_t player, s_section* section) {
            const s_player& me = MOX::player[player];
            const int32_t rp = me.research_produced == 0x7FFF ? 0 : me.research_produced;  // the game stores 0x7FFF for a special case
            const int32_t app = static_cast<uint8_t>(me.current_research_application);
            const char* name = app < TECH_APP_COUNT ? TECHDATA::technology_applications[app].name : nullptr;
            if (name != nullptr && name[0] != '\0') {
                section->lines.push_back(Text_("mentat.research.line", {{"rp", Num_(rp)}, {"tech", name}}));
            } else {
                section->lines.push_back(Text_("mentat.research.none", {{"rp", Num_(rp)}}));
            }
        }

        void Fill_Espionage_(int16_t player, s_section* section) {
            std::vector<std::string> parts;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Alive_(o) || MOX::player[player].contact[o] == 0) continue;
                const int32_t points = espionage::Points(player, o);
                if (points > 0) parts.push_back(Name_(o) + " " + Num_(points));
            }
            section->lines.push_back(parts.empty() ? Text_("mentat.espionage.none") : Text_("mentat.espionage.network", {{"parts", Join_(parts)}}));
            int32_t caught = 0;
            for (const s_MSG* msg : Messages_(player)) caught += msg->msg_type == MSG_TYPE_LOST_ATTACKING_SPY ? 1 : 0;
            if (caught > 0) section->lines.push_back(Text_("mentat.espionage.caught", {{"count", Num_(caught)}}));
        }

        void Fill_Diplomacy_(int16_t player, const s_memory& mem, s_section* section) {
            Diplomacy_Events_(player, &section->lines);
            struct s_change {
                int32_t other;
                int32_t from;
                int32_t to;
            };
            std::vector<s_change> changes;
            int32_t coldest = -1;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Alive_(o) || MOX::player[player].contact[o] == 0) continue;
                const int32_t now = MOX::player[player].relations[o];
                if (mem.valid && mem.relations[o] != now) changes.push_back({o, mem.relations[o], now});
                if (coldest < 0 || now < MOX::player[player].relations[coldest]) coldest = o;
            }
            std::sort(changes.begin(), changes.end(), [](const s_change& l, const s_change& r) {
                return std::abs(l.to - l.from) > std::abs(r.to - r.from);
            });
            for (size_t i = 0; i < changes.size() && i < 2; ++i) {
                section->lines.push_back(Text_("mentat.diplomacy.change", {{"name", Name_(changes[i].other)}, {"from", Num_(changes[i].from)}, {"to", Num_(changes[i].to)}}));
            }
            if (changes.empty() && coldest >= 0) {
                section->lines.push_back(Text_("mentat.diplomacy.coldest", {{"name", Name_(coldest)}, {"value", Num_(MOX::player[player].relations[coldest])}}));
            }
        }

        void Fill_Military_(int16_t player, s_section* section) {
            int32_t listed = 0;
            if (MOX::ship != nullptr) {
                for (const int32_t star : Colony_Stars_(player)) {
                    int32_t mine = 0;
                    int32_t theirs = 0;
                    bool war = false;
                    for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                        const s_ship_data& ship = MOX::ship[i];
                        if (!Ship_Alive_(ship)) continue;
                        const int32_t strength = Ship_Strength_(ship);
                        if (strength == 0) continue;
                        if (ship.owner == player) {
                            if (ship.status == 0 && ship.location == star) mine += strength;
                        } else if (Near_(ship, star) && SHIPSTAK::Is_Ship_Detected_By(static_cast<int16_t>(i), static_cast<uint8_t>(player))) {
                            theirs += strength;
                            war = war || ship.owner == ANTARAN_OWNER || At_War_(player, ship.owner);
                        }
                    }
                    if (theirs == 0 || listed >= 3) continue;
                    ++listed;
                    section->lines.push_back(Text_(war ? "mentat.military.near_war" : "mentat.military.near",
                        {{"star", Star_Name_(star)}, {"theirs", Num_(theirs)}, {"mine", Num_(mine)}}));
                }
            }
            if (listed == 0) section->lines.push_back(Text_("mentat.military.none"));
        }

        // What the neighbours remember about you, the two heaviest reasons each (detailed only).
        void Opinion_Lines_(int16_t player, std::vector<s_text>* lines) {
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Alive_(o) || MOX::player[player].contact[o] == 0) continue;
                int32_t first = -1;
                int32_t second = -1;
                for (int32_t r = 0; r < opinion::REASON_RESERVED_30; ++r) {
                    const int32_t value = opinion::Modifier(o, player, static_cast<opinion::e_reason>(r));
                    if (std::abs(value) < OPINION_MIN) continue;
                    if (first < 0 || std::abs(value) > std::abs(opinion::Modifier(o, player, static_cast<opinion::e_reason>(first)))) {
                        second = first;
                        first = r;
                    } else if (second < 0 || std::abs(value) > std::abs(opinion::Modifier(o, player, static_cast<opinion::e_reason>(second)))) {
                        second = r;
                    }
                }
                if (first < 0) continue;
                std::vector<std::string> parts;
                for (const int32_t r : {first, second}) {
                    if (r < 0) continue;
                    const int32_t value = opinion::Modifier(o, player, static_cast<opinion::e_reason>(r));
                    const std::string key = "mentat.reason." + Num_(r);
                    parts.push_back(std::string(texts::Get(key.c_str())) + " " + (value > 0 ? "+" : "") + Num_(value));
                }
                lines->push_back(Text_("mentat.why.opinion", {{"name", Name_(o)}, {"parts", Join_(parts)}}));
            }
        }
    } // namespace

    // ---- the public interface ----

    bool Available() {
        if (mode == MODE_OFF) return false;
        return !(MOX::game_type == 2 || MOX::game_type == 3 || NETMOX::net.is_network_active != 0);
    }

    std::string Game(const s_text& text) {
        return texts::Expand(text.key.c_str(), text.args, false);
    }

    std::string Utf8(const s_text& text) {
        texts::t_args args;
        for (const auto& arg : text.args) args.emplace_back(arg.first, text_language::To_Utf8(arg.second));
        return texts::Expand(text.key.c_str(), args, true);
    }

    bool Check(e_alert kind, int16_t player, s_alert* out, uint8_t level) {
        if (out == nullptr || player < 0 || player >= Players_() || MOX::player == nullptr) return false;
        s_alert found;
        bool stands = false;
        switch (kind) {
            case ALERT_WAR: stands = Check_War_(player, &found); break;
            case ALERT_FLEET: stands = Check_Fleet_(player, &found); break;
            case ALERT_COUNCIL_LOSE: stands = level >= DETAIL_DETAILED && Check_Council_Lose_(player, &found); break;
            case ALERT_COUNCIL_SHORT: stands = level >= DETAIL_DETAILED && Check_Council_Short_(player, &found); break;
            case ALERT_UNREST: stands = Check_Unrest_(player, &found); break;
            case ALERT_SPY: stands = Check_Spy_(player, &found); break;
            case ALERT_ULTIMATUM: stands = Check_Ultimatum_(player, &found); break;
            case ALERT_PRISONER: stands = Check_Prisoner_(player, &found); break;
            case ALERT_BANKRUPTCY: stands = Check_Bankruptcy_(player, &found); break;
            case ALERT_WEARINESS: stands = Check_Weariness_(player, &found); break;
            case ALERT_CRISIS: stands = Check_Crisis_(player, &found); break;
            case ALERT_ANTARANS: stands = Check_Antarans_(player, &found); break;
            default: break;
        }
        if (!stands) return false;
        found.kind = kind;
        *out = std::move(found);
        return true;
    }

    std::vector<s_alert> Find_Alerts(int16_t player, uint8_t level) {
        std::vector<s_alert> found;
        for (int32_t kind = 0; kind < ALERT_COUNT; ++kind) {
            s_alert alert;
            if (Check(static_cast<e_alert>(kind), player, &alert, level)) found.push_back(std::move(alert));
        }
        return found;
    }

    void Build_Summary(int16_t player, uint8_t detail_level, s_summary* out) {
        *out = s_summary{};
        if (player < 0 || player >= Players_() || MOX::player == nullptr) return;
        const s_memory& mem = memory[player];
        out->stardate = MOX::stardate;
        out->player = player;
        out->detail = detail_level;
        out->alerts = Find_Alerts(player, detail_level);
        if (mode == MODE_ADVICE) Build_Advice(player, detail_level, &out->advice);
        if (detail_level == DETAIL_BRIEF) {
            s_section section{"mentat.section.summary", {}};
            Fill_Brief_(player, detail_level, mem, &section);
            out->sections.push_back(std::move(section));
            return;
        }
        s_section summary{"mentat.section.summary", {}};
        Fill_Brief_(player, detail_level, mem, &summary);
        // The medium level goes by field; the brief's first lines stay the opening of the turn.
        out->sections.push_back(std::move(summary));
        s_section economy{"mentat.section.economy", {}};
        Fill_Economy_(player, mem, &economy);
        out->sections.push_back(std::move(economy));
        s_section research{"mentat.section.research", {}};
        Fill_Research_(player, &research);
        out->sections.push_back(std::move(research));
        s_section espionage_section{"mentat.section.espionage", {}};
        Fill_Espionage_(player, &espionage_section);
        out->sections.push_back(std::move(espionage_section));
        s_section diplomacy{"mentat.section.diplomacy", {}};
        Fill_Diplomacy_(player, mem, &diplomacy);
        out->sections.push_back(std::move(diplomacy));
        s_section military{"mentat.section.military", {}};
        Fill_Military_(player, &military);
        out->sections.push_back(std::move(military));
        if (detail_level >= DETAIL_DETAILED) {
            for (const s_alert& alert : out->alerts) out->why.push_back(alert.why);
            Opinion_Lines_(player, &out->why);
        }
    }

    void Clear() {
        for (s_memory& mem : memory) mem = s_memory{};
        current = s_summary{};
        open_request = false;
        opens_panel = false;
        alert_shown = false;
        seen = true;
    }

    void Turn_Start(int16_t player) {
        if (!Available() || !Human_(player)) {
            current = s_summary{};
            open_request = false;
            opens_panel = false;
            return;
        }
        if (current.stardate == MOX::stardate && current.player == player) return;
        Build_Summary(player, detail <= DETAIL_DETAILED ? detail : DETAIL_BRIEF, &current);
        alert_shown = false;
        seen = false;
        switch (open_when) {
            case OPEN_TURN: open_request = true; break;
            case OPEN_PERIOD: open_request = period > 0 && (MOX::stardate - 35000) % period == 0; break;
            case OPEN_CRITICAL: open_request = !current.alerts.empty(); break;
            default: open_request = false; break;
        }
        opens_panel = open_request;
        // The figures the next summary compares with.
        s_memory& mem = memory[player];
        mem.valid = true;
        mem.stardate = MOX::stardate;
        mem.bc = MOX::player[player].bc;
        mem.income = MOX::player[player].surplus_bc;
        for (int16_t o = 0; o < MAX_PLAYERS; ++o) mem.relations[o] = MOX::player[player].relations[o];
        const news::s_ratings& ratings = news::Ratings();
        if (ratings.stardate != 0 && ratings.stardate != mem.ratings_stardate) {
            mem.ratings_stardate = ratings.stardate;
            for (int32_t r = 0; r < RATINGS; ++r) mem.rank[r] = Visible_Rank_(r, player);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "mentat: stardate %d, player %d: %d threats, %d lines, panel %s", (int)MOX::stardate, (int)player,
            (int)current.alerts.size(), (int)current.sections.size(), open_request ? "opens" : "stays as it is");
    }

    const s_summary* Current() {
        return current.stardate >= 0 ? &current : nullptr;
    }

    bool Take_Open_Request() {
        const bool request = open_request;
        open_request = false;
        return request;
    }

    int32_t New_Count() {
        if (current.stardate < 0 || seen) return 0;
        int32_t count = static_cast<int32_t>(current.alerts.size());
        for (const s_section& section : current.sections) count += static_cast<int32_t>(section.lines.size());
        return count;
    }

    void Mark_Seen() {
        seen = true;
    }

    std::string Alert_Text(const s_summary& summary) {
        if (summary.alerts.empty()) return std::string();
        const size_t more = summary.alerts.size() - 1;
        return texts::Expand("mentat.alert.box",
            {{"alert", Game(summary.alerts[0].text)}, {"more", more > 0 ? " (+" + Num_(static_cast<int64_t>(more)) + ")" : std::string()}}, false);
    }

    int8_t Alert_Step(int16_t player, bool show_ui) {
        if (!Available() || !alerts || alert_shown || !Human_(player)) return 0;
        if (current.stardate != MOX::stardate || current.player != player || current.alerts.empty()) return 0;
        if (opens_panel || debug_overlay::Mentat_Visible()) return 0;  // the panel is (or will be) open: it shows the threats
        if (!show_ui) return 1;
        alert_shown = true;
        const std::string line = Alert_Text(current);
        GENDRAW::Message_Box(line.c_str());
        return 1;
    }

} // namespace mentat
