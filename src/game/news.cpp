#include "pch.h"

#include "game/news.h"

#include "game/beasts.h"
#include "game/bill.h"
#include "game/crises.h"
#include "game/random.h"
#include "game/techdata.h"

#include <algorithm>
#include <cstdio>

// The behaviour of news and ratings (events-catalog 3.12); the ring, the ranking and the save section are in
// news_state.cpp. game_random is drawn only with events news and news_intelligence on, one draw for each
// (reader, subject) pair where the reader has spies and the subject's original AI plans an attack.
namespace news {

    namespace {
        constexpr int32_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;  // as advisor_view.cpp

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        int32_t Spies_(int16_t reader, int16_t subject) {
            return BILL::Get_Their_Spy_Number(reader, subject);
        }

        // A warship's weight: 1 for the hull and the damage its weapons can do (as advisor_intel.cpp counts them).
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
            return ship.owner >= 0 && ship.owner < MAX_PLAYERS && ship.status < SHIP_STATUS_FIRST_NOT_ALIVE;
        }

        int32_t Techs_Known_(int16_t player) {
            int32_t n = 0;
            for (int32_t i = 0; i < TECH_APP_COUNT; ++i) {
                if (MOX::player[player].tech_applications[i] == TECH_RESEARCH_STATUS_RESEARCHED) ++n;
            }
            return n;
        }

        std::string Format_(const char* format, int a = 0, int b = 0, int c = 0, int d = 0) {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), format, a, b, c, d);
            return buffer;
        }

        void Log_(const s_item& item) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "news: %s%s", Describe(item).c_str(), item.reader >= 0 ? " (private)" : "");
        }

        bool Intelligence_Ready_(int16_t reader, int16_t subject) {
            return enabled && intelligence && Alive_(reader) && Alive_(subject) && reader != subject && Spies_(reader, subject) > 0;
        }

        // Ranks the living empires by `figure[p]`, best first, ties by the lower index.
        void Rank_(const int32_t* figure, int8_t* order, int32_t* value) {
            int16_t living[RANK_SLOTS];
            int32_t n = 0;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (Alive_(p)) living[n++] = p;
            }
            std::stable_sort(living, living + n, [&](int16_t a, int16_t b) { return figure[a] > figure[b]; });
            for (int32_t i = 0; i < RANK_SLOTS; ++i) {
                order[i] = i < n ? static_cast<int8_t>(living[i]) : static_cast<int8_t>(-1);
                value[i] = i < n ? figure[living[i]] : 0;
            }
        }

        // Eliminations since the last look; the first look (after a start or a load) is only the baseline.
        void Notice_Eliminations_() {
            s_watch* w = Watch();
            uint8_t now = 0;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (MOX::player[p].eliminated != 0) now |= static_cast<uint8_t>(1u << p);
            }
            if (w->seeded) {
                for (int16_t p = 0; p < Players_(); ++p) {
                    if ((now & (1u << p)) != 0 && (w->eliminated & (1u << p)) == 0) Add(KIND_ELIMINATED, p, -1, 0);
                }
            }
            w->seeded = true;
            w->eliminated = now;
        }
    } // namespace

    void Add(e_kind kind, int16_t subject, int16_t object, int32_t value, int16_t reader) {
        if (!enabled || kind == KIND_NONE || kind >= KIND_COUNT) return;
        s_item item;
        item.stardate = MOX::stardate;
        item.kind = kind;
        item.subject = static_cast<int8_t>(subject);
        item.object = static_cast<int8_t>(object);
        item.reader = static_cast<int8_t>(reader);
        item.value = static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
        Push_Item(item);
        Log_(item);
    }

    bool Visible_To(const s_item& item, int16_t viewer) {
        if (viewer < 0 || viewer >= Players_()) return false;
        if (item.reader >= 0) return item.reader == viewer;
        if (item.kind == KIND_RATINGS || item.kind == KIND_COUNCIL) return true;
        bool named = false;
        for (const int8_t other : {item.subject, item.object}) {
            if (other < 0 || other >= MAX_PLAYERS) continue;
            named = true;
            if (other == viewer || MOX::player[viewer].contact[other] != 0) return true;
        }
        return !named;  // a galaxy-wide fact
    }

    int32_t Recent_For(int16_t viewer, s_item* out, int32_t max) {
        int32_t n = 0;
        if (!enabled || out == nullptr) return 0;
        for (int32_t age = 0; age < Count() && n < max; ++age) {
            const s_item* item = Nth_Newest(age);
            if (Visible_To(*item, viewer)) out[n++] = *item;
        }
        return n;
    }

    std::string Describe(const s_item& item) {
        const std::string head = Format_("stardate %d: ", item.stardate);
        const int s = item.subject;
        const int o = item.object;
        switch (item.kind) {
            case KIND_WAR: return head + Format_("player %d declared war on player %d", s, o);
            case KIND_PEACE: return head + Format_("player %d and player %d made peace", s, o);
            case KIND_ALLIANCE: return head + Format_("player %d and player %d formed an alliance", s, o);
            case KIND_COLONY_TAKEN: return head + Format_("player %d took colony %d from player %d by invasion", s, item.value, o);
            case KIND_COLONY_FOUNDED: return head + Format_("player %d founded colony %d", s, item.value);
            case KIND_ELIMINATED: return head + Format_("player %d was eliminated", s);
            case KIND_COUNCIL:
                return head + (s >= 0 ? Format_("the council elected player %d", s) : std::string("the council elected nobody"));
            case KIND_EVENT:
                return head + (s >= 0 ? Format_("an event (type %d) began for player %d", item.value, s)
                                      : Format_("a galactic event (type %d) began", item.value));
            case KIND_CRISIS_START: {
                const char* name = crises::Name(static_cast<crises::e_crisis>(item.value));
                return head + Format_("player %d faces a crisis: ", s) + name;
            }
            case KIND_CRISIS_END: {
                static const char* const RESULTS[4] = {"unresolved", "success", "partial success", "failure"};
                const char* name = crises::Name(static_cast<crises::e_crisis>(item.value / 10));
                return head + Format_("player %d's crisis ended: ", s) + name + " - " + RESULTS[std::clamp(item.value % 10, 0, 3)];
            }
            case KIND_RATINGS:
                return head + Format_("ratings: player %d leads in population, player %d in fleet, player %d in technology", s, o, item.value);
            case KIND_FLEETS_GATHER: return head + Format_("player %d's fleets gather at star %d", s, item.value);
            case KIND_EYES_ON: return head + Format_("player %d has its eyes on player %d", s, o);
            case KIND_WORMHOLE: return head + Format_("player %d holds a wormhole that leads to star %d", s, item.value);
            default: return head + "unknown";
        }
    }

    std::string Describe_Ratings(int16_t viewer) {
        const s_ratings& ratings = Ratings();
        if (ratings.stardate == 0 || viewer < 0 || viewer >= Players_()) return std::string();
        static const char* const NAMES[RATING_COUNT] = {"population", "fleet", "technologies"};
        std::string text = Format_("stardate %d: ", ratings.stardate);
        for (int32_t r = 0; r < RATING_COUNT; ++r) {
            text += r == 0 ? "" : "; ";
            text += NAMES[r];
            bool first = true;
            for (int32_t i = 0; i < RANK_SLOTS; ++i) {
                const int32_t p = ratings.order[r][i];
                if (p < 0) break;
                if (p != viewer && MOX::player[viewer].contact[p] == 0) continue;
                text += first ? " " : " > ";
                text += Format_("%d", p);
                first = false;
            }
        }
        return text;
    }

    void Make_Ratings() {
        if (!enabled) return;
        int32_t figure[RATING_COUNT][RANK_SLOTS] = {};
        for (int16_t p = 0; p < Players_(); ++p) {
            figure[RATING_POPULATION][p] = MOX::player[p].total_pop;
            figure[RATING_TECHNOLOGY][p] = Techs_Known_(p);
        }
        if (MOX::ship != nullptr) {
            for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (Ship_Alive_(ship) && ship.owner < Players_()) figure[RATING_FLEET][ship.owner] += Ship_Strength_(ship);
            }
        }
        s_ratings ratings;
        ratings.stardate = MOX::stardate;
        for (int32_t r = 0; r < RATING_COUNT; ++r) Rank_(figure[r], ratings.order[r], ratings.value[r]);
        if (ratings.order[0][0] < 0) return;  // nobody lives
        Set_Ratings(ratings);
        Add(KIND_RATINGS, ratings.order[RATING_POPULATION][0], ratings.order[RATING_FLEET][0], ratings.order[RATING_TECHNOLOGY][0]);
    }

    int32_t Gathering_Star(int16_t subject) {
        if (!Alive_(subject) || MOX::ship == nullptr) return -1;
        static int32_t at_star[MAX_STARS];
        std::fill(std::begin(at_star), std::end(at_star), 0);
        int32_t total = 0;
        for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
            const s_ship_data& ship = MOX::ship[i];
            if (!Ship_Alive_(ship) || ship.owner != subject) continue;
            const int32_t strength = Ship_Strength_(ship);
            if (strength == 0) continue;
            total += strength;
            if (ship.status == 0 && ship.location >= 0 && ship.location < MAX_STARS) at_star[ship.location] += strength;
        }
        int32_t best = -1;
        for (int32_t star = 0; star < MAX_STARS; ++star) {
            if (at_star[star] > 0 && (best < 0 || at_star[star] > at_star[best])) best = star;
        }
        if (best < 0 || at_star[best] * GATHER_DIVISOR < total) return -1;
        return best;
    }

    int16_t Planned_Victim(int16_t subject) {
        if (!Alive_(subject)) return -1;
        const int16_t victim = MOX::player[subject].sneak_attack_player;
        return Alive_(victim) && victim != subject ? victim : -1;
    }

    bool Report_Gathering(int16_t reader, int16_t subject) {
        if (!Intelligence_Ready_(reader, subject)) return false;
        const int32_t star = Gathering_Star(subject);
        if (star < 0) return false;
        s_item probe;
        probe.kind = KIND_FLEETS_GATHER;
        probe.subject = static_cast<int8_t>(subject);
        probe.reader = static_cast<int8_t>(reader);
        probe.value = static_cast<int16_t>(star);
        if (Contains(probe)) return false;  // the reader already knows
        Add(KIND_FLEETS_GATHER, subject, -1, star, reader);
        return true;
    }

    bool Report_Eyes_On(int16_t reader, int16_t subject, int32_t roll) {
        if (!Intelligence_Ready_(reader, subject)) return false;
        const int16_t victim = Planned_Victim(subject);
        if (victim < 0) return false;
        const int32_t chance = EYES_ON_PER_MILLE_PER_SPY * std::min<int32_t>(MAX_SPIES, Spies_(reader, subject));
        if (roll >= chance) return false;
        s_item probe;
        probe.kind = KIND_EYES_ON;
        probe.subject = static_cast<int8_t>(subject);
        probe.object = static_cast<int8_t>(victim);
        probe.reader = static_cast<int8_t>(reader);
        if (Contains(probe)) return false;
        Add(KIND_EYES_ON, subject, victim, 0, reader);
        return true;
    }

    bool Report_Wormhole(int16_t reader, int16_t subject) {
        if (!Intelligence_Ready_(reader, subject) || !beasts::enabled) return false;
        const beasts::s_state* bs = beasts::State();
        for (int32_t i = 0; i < bs->count; ++i) {
            const beasts::s_record& r = bs->record[i];
            if (r.kind != beasts::KIND_WORMHOLE || r.player != subject) continue;
            s_item probe;
            probe.kind = KIND_WORMHOLE;
            probe.subject = static_cast<int8_t>(subject);
            probe.reader = static_cast<int8_t>(reader);
            probe.value = r.other;
            if (Contains(probe)) return false;
            Add(KIND_WORMHOLE, subject, -1, r.other, reader);
            return true;
        }
        return false;
    }

    void Turn() {
        if (!enabled) return;
        Notice_Eliminations_();
        if (MOX::stardate % RATING_PERIOD == 0) Make_Ratings();
        if (!intelligence || MOX::stardate % INTEL_PERIOD != INTEL_PHASE) return;
        for (int16_t reader = 0; reader < Players_(); ++reader) {
            for (int16_t subject = 0; subject < Players_(); ++subject) {
                if (!Intelligence_Ready_(reader, subject)) continue;
                Report_Gathering(reader, subject);
                Report_Wormhole(reader, subject);
                if (Planned_Victim(subject) >= 0) {
                    Report_Eyes_On(reader, subject, static_cast<int32_t>(game_random::Random(1000)));
                }
            }
        }
    }

} // namespace news
