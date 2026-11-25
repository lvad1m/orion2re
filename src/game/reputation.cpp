#include "pch.h"

#include "game/reputation.h"

#include "game/crises.h"
#include "game/opinion.h"

#include <algorithm>

// Deeds and reviews of stains and titles (mechanics.md 3.5.6, events-catalog.md 3.12); the state,
// what titles add to relations and the save section are in reputation_state.cpp. Nothing here draws
// from game_random.
namespace reputation {

    namespace {
        constexpr int8_t TREATY_NAP = 1;
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_PEACE = 3;
        constexpr int8_t TREATY_FIRST_WAR = 4;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        void Grant_(int16_t player, e_title title, int32_t turns) {
            int32_t& until = State()->until[player][title];
            const bool had = until > MOX::stardate;
            until = std::max(until, MOX::stardate + turns);
            if (!had) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "reputation: stardate %d, player %d is now known as %s",
                    (int)MOX::stardate, (int)player, Title_Name(title));
            }
        }

        void Revoke_(int16_t player, e_title title) {
            int32_t& until = State()->until[player][title];
            if (until > MOX::stardate) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "reputation: stardate %d, player %d is no longer known as %s",
                    (int)MOX::stardate, (int)player, Title_Name(title));
            }
            until = 0;
        }

        // Wars that ended since last turn are peaces made by both sides.
        void Note_Peaces_() {
            s_state* s = State();
            for (int16_t a = 0; a < Players_(); ++a) {
                uint8_t now = 0;
                for (int16_t b = 0; b < Players_(); ++b) {
                    if (b != a && Alive_(a) && Alive_(b) && MOX::player[a].treaty[b] >= TREATY_FIRST_WAR) now = static_cast<uint8_t>(now | (1u << b));
                }
                for (int16_t b = 0; b < Players_(); ++b) {
                    const bool was = (s->at_war[a] >> b) & 1;
                    const bool is = (now >> b) & 1;
                    if (was && !is && Alive_(a) && Alive_(b)) {
                        s->peaces[a][1] = s->peaces[a][0];
                        s->peaces[a][0] = MOX::stardate;
                    }
                }
                s->at_war[a] = now;
            }
        }

        void Review_Conduct_() {
            s_state* s = State();
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                const bool clean = s->breaches[p][0] == 0 || MOX::stardate - s->breaches[p][0] >= GOOD_CONDUCT_WINDOW;
                int32_t kept = 0;
                for (int16_t o = 0; o < Players_(); ++o) {
                    const int8_t t = MOX::player[p].treaty[o];
                    if (o != p && Alive_(o) && (t == TREATY_NAP || t == TREATY_ALLIANCE || t == TREATY_PEACE) &&
                        MOX::player[p].peace_duration[o] >= KEPT_TREATY_TURNS) {
                        ++kept;
                    }
                }
                if (clean && kept >= 2) {
                    Grant_(p, TITLE_KEEPS_WORD, PERIOD + 1);
                } else {
                    Revoke_(p, TITLE_KEEPS_WORD);
                }
                const bool peacemaker = s->peaces[p][1] != 0 && MOX::stardate - s->peaces[p][1] < GOOD_CONDUCT_WINDOW;
                if (peacemaker) {
                    Grant_(p, TITLE_PEACEMAKER, PERIOD + 1);
                } else {
                    Revoke_(p, TITLE_PEACEMAKER);
                }
            }
        }

        int32_t Fleet_(int16_t p) {
            int32_t best = 0;
            for (int16_t o = 0; o < Players_(); ++o) best = std::max(best, MOX::player[p].total_ship_strength[o]);
            return best;
        }

        int32_t Trade_Treaties_(int16_t p) {
            int32_t n = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != p && MOX::player[p].trade_treaty[o] != 0) ++n;
            }
            return n;
        }

        // The one leader of the galaxy in `value`, if it reaches `minimum` and leads the second by
        // `lead_percent`; -1 otherwise.
        template <typename F>
        int16_t Leader_(F value, int32_t minimum, int32_t lead_percent) {
            int16_t best = -1;
            int32_t first = 0;
            int32_t second = 0;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                const int32_t v = value(p);
                if (v > first) {
                    second = first;
                    first = v;
                    best = p;
                } else if (v > second) {
                    second = v;
                }
            }
            if (best < 0 || first < minimum || static_cast<int64_t>(first) * 100 <= static_cast<int64_t>(second) * lead_percent) return -1;
            return best;
        }

        void Review_Style_() {
            s_state* s = State();
            const int16_t prince = Leader_([](int16_t p) { return Trade_Treaties_(p); }, TRADE_PRINCE_MIN_TREATIES, 100);
            const int16_t warlord = Leader_([](int16_t p) { return Fleet_(p); }, 1, WARLORD_LEAD_PERCENT);
            const int16_t corsair = Leader_([s](int16_t p) { return static_cast<int32_t>(s->deeds[p]); }, CORSAIR_MIN_DEEDS, 100);
            for (int16_t p = 0; p < Players_(); ++p) {
                if (p == prince) Grant_(p, TITLE_TRADE_PRINCE, STYLE_PERIOD + 1); else Revoke_(p, TITLE_TRADE_PRINCE);
                if (p == warlord) Grant_(p, TITLE_WARLORD, STYLE_PERIOD + 1); else Revoke_(p, TITLE_WARLORD);
                if (p == corsair) Grant_(p, TITLE_CORSAIR, STYLE_PERIOD + 1); else Revoke_(p, TITLE_CORSAIR);
            }
            if (MOX::stardate - s->deeds_window >= DEEDS_WINDOW) {
                for (int16_t& d : s->deeds) d = 0;
                s->deeds_window = MOX::stardate;
            }
        }
    } // namespace

    void On_Treaty_Broken(int16_t breaker, int16_t victim, int32_t treaty) {
        if (!enabled || !Alive_(breaker) || !Alive_(victim) || breaker == victim) return;
        if (treaty != TREATY_NAP && treaty != TREATY_ALLIANCE && treaty != TREATY_PEACE) return;
        s_state* s = State();
        s->breaches[breaker][1] = s->breaches[breaker][0];
        s->breaches[breaker][0] = MOX::stardate;
        opinion::Add(victim, breaker, opinion::REASON_BROKE_ITS_WORD, BROKE_WORD_OPINION);
        const bool twice = s->breaches[breaker][1] != 0 && MOX::stardate - s->breaches[breaker][1] < BREACH_WINDOW;
        if (treaty == TREATY_ALLIANCE || twice) Grant_(breaker, TITLE_OATHBREAKER, OATHBREAKER_TURNS);
        Revoke_(breaker, TITLE_KEEPS_WORD);
    }

    void On_Atrocity(int16_t player) {
        crises::Add_Threat(player);  // "become a crisis": the threat scale (events crisis_front, off by default)
        if (!enabled || !Alive_(player)) return;
        Grant_(player, TITLE_ATROCITIES, ATROCITY_TURNS);
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o != player && Alive_(o) && MOX::player[o].contact[player] != 0) {
                opinion::Add(o, player, opinion::REASON_ATROCITY, ATROCITY_OPINION);
            }
        }
    }

    void On_Leader_Killed(int16_t player) {
        if (!enabled || !Alive_(player)) return;
        Grant_(player, TITLE_KINGSLAYER, KINGSLAYER_TURNS);
    }

    void Note_Deed(int16_t player) {
        if (!enabled || !Alive_(player)) return;
        int16_t& d = State()->deeds[player];
        if (d < 30000) ++d;
    }

    void Turn() {
        if (!enabled) return;
        Note_Peaces_();
        if (MOX::stardate % PERIOD == 0) Review_Conduct_();
        if (MOX::stardate % STYLE_PERIOD == 0) Review_Style_();
    }

} // namespace reputation
