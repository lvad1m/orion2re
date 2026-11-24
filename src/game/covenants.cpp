#include "pch.h"

#include "game/covenants.h"

#include "game/bill.h"
#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"
#include "game/random.h"

#include <algorithm>

// The behaviour of secret clauses and dynastic marriages (mechanics.md 3.5.3); the state and its save
// section are in covenants_state.cpp. game_random is drawn only with diplomacy secret_clauses (the
// spies' rolls) or diplomacy marriages (the match) on, and Declare_War draws when a guarantor answers.
namespace covenants {

    namespace {
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;
        bool answering = false;  // Declare_War leads back to On_Aggression

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool At_War_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] >= TREATY_FIRST_WAR;
        }

        bool In_Contact_(int16_t a, int16_t b) {
            return MOX::player[a].contact[b] != 0 && MOX::player[b].contact[a] != 0;
        }

        // Clauses: the pair of the clause `i` is {guarantor, protege} against its target.
        bool Same_Pair_(const s_clause& x, const s_clause& y) {
            return x.target == y.target && ((x.guarantor == y.guarantor && x.protege == y.protege) || (x.guarantor == y.protege && x.protege == y.guarantor));
        }

        bool Pair_Exists_(int16_t a, int16_t b, int16_t c) {
            return Find_Clause(a, b, c) >= 0 || Find_Clause(b, a, c) >= 0;
        }

        bool Expired_(const s_clause& c) {
            return MOX::stardate - c.since >= CLAUSE_TURNS;
        }

        void Review_Clauses_() {
            s_state* s = State();
            for (int32_t i = 0; i < s->count;) {
                const s_clause c = s->clauses[i];
                const char* why = nullptr;
                if (!Alive_(c.guarantor) || !Alive_(c.protege) || !Alive_(c.target)) {
                    why = "a party is gone";
                } else if (At_War_(c.guarantor, c.protege)) {
                    why = "the partners are at war";
                } else if (Expired_(c)) {
                    why = "it ran out";
                }
                if (why != nullptr) {
                    End_Clause(i, why);
                } else {
                    ++i;
                }
            }
        }

        void Sign_Clauses_() {
            const int16_t players = Players_();
            for (int16_t a = 0; a < players; ++a) {
                if (!Computer_(a)) continue;
                bool signed_one = false;
                for (int16_t b = 0; b < players && !signed_one; ++b) {
                    if (b == a || !Computer_(b)) continue;
                    for (int16_t c = 0; c < players; ++c) {
                        if (!Can_Sign(a, b, c)) continue;
                        if (Sign(a, b, c)) signed_one = true;
                        break;
                    }
                }
            }
        }

        void Keep_Marriages_() {
            const int16_t players = Players_();
            for (int16_t a = 0; a < players; ++a) {
                const int16_t b = State()->spouse[a];
                if (b < 0 || b < a) continue;
                if (!Alive_(a) || !Alive_(b)) {
                    Divorce(a, b, -1, "a spouse is gone");
                } else if (At_War_(a, b)) {
                    Divorce(a, b, -1, "they are at war");
                } else {
                    if (opinion::Modifier(a, b, opinion::REASON_FAMILY_TIES) < FAMILY_OPINION) opinion::Set(a, b, opinion::REASON_FAMILY_TIES, FAMILY_OPINION);
                    if (opinion::Modifier(b, a, opinion::REASON_FAMILY_TIES) < FAMILY_OPINION) opinion::Set(b, a, opinion::REASON_FAMILY_TIES, FAMILY_OPINION);
                }
            }
        }

        void Make_Marriages_() {
            const int16_t players = Players_();
            for (int16_t a = 0; a < players; ++a) {
                for (int16_t b = static_cast<int16_t>(a + 1); b < players; ++b) {
                    if (State()->spouse[a] >= 0) break;
                    if (!Can_Marry(a, b)) continue;
                    if (static_cast<int32_t>(game_random::Random(100)) < MARRY_PERCENT) Marry(a, b);
                }
            }
        }
    } // namespace

    bool Can_Sign(int16_t a, int16_t b, int16_t c) {
        if (!secret_clauses || a == b || a == c || b == c) return false;
        if (!Computer_(a) || !Computer_(b) || !Alive_(c)) return false;
        if (!In_Contact_(a, b) || MOX::player[a].contact[c] == 0 || MOX::player[b].contact[c] == 0) return false;
        if (At_War_(a, b) || MOX::player[a].treaty[b] == TREATY_ALLIANCE) return false;
        if (MOX::player[a].treaty[c] == TREATY_ALLIANCE || MOX::player[b].treaty[c] == TREATY_ALLIANCE) return false;
        if (opinion::Relations(a, b) < SIGN_FRIENDSHIP) return false;
        if (opinion::Relations(a, c) > SIGN_HATRED || opinion::Relations(b, c) > SIGN_HATRED) return false;
        return !Pair_Exists_(a, b, c);
    }

    bool Sign(int16_t a, int16_t b, int16_t c) {
        if (!Can_Sign(a, b, c)) return false;
        s_state* s = State();
        if (s->count + 2 > MAX_CLAUSES) return false;
        s->clauses[s->count++] = {static_cast<int8_t>(a), static_cast<int8_t>(b), static_cast<int8_t>(c), 0, MOX::stardate};
        s->clauses[s->count++] = {static_cast<int8_t>(b), static_cast<int8_t>(a), static_cast<int8_t>(c), 0, MOX::stardate};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, players %d and %d sign a secret clause against player %d",
            (int)MOX::stardate, (int)a, (int)b, (int)c);
        return true;
    }

    void End_Clause(int32_t index, const char* why) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        const s_clause c = s->clauses[index];
        for (int32_t i = index; i + 1 < s->count; ++i) s->clauses[i] = s->clauses[i + 1];
        s->clauses[--s->count] = s_clause{};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, the clause of player %d guarding player %d against player %d ends: %s",
            (int)MOX::stardate, (int)c.guarantor, (int)c.protege, (int)c.target, why != nullptr ? why : "");
    }

    void Reveal(int32_t index, const char* how) {
        s_state* s = State();
        if (index < 0 || index >= s->count || s->clauses[index].revealed != 0) return;
        const s_clause c = s->clauses[index];
        for (int32_t i = 0; i < s->count; ++i) {
            if (Same_Pair_(s->clauses[i], c)) s->clauses[i].revealed = 1;
        }
        opinion::Add(c.target, c.guarantor, opinion::REASON_PLOTTED_AGAINST_US, REVEALED_OPINION);
        opinion::Add(c.target, c.protege, opinion::REASON_PLOTTED_AGAINST_US, REVEALED_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, player %d learns of the clause of players %d and %d (%s)",
            (int)MOX::stardate, (int)c.target, (int)c.guarantor, (int)c.protege, how != nullptr ? how : "");
    }

    bool Spy_Reveal_With_Roll(int32_t index, int32_t roll) {
        if (!secret_clauses) return false;
        s_state* s = State();
        if (index < 0 || index >= s->count || s->clauses[index].revealed != 0) return false;
        const s_clause& c = s->clauses[index];
        if (!Alive_(c.target)) return false;
        const int32_t spies = std::min<int32_t>(SPY_CAP, BILL::Get_Their_Spy_Number(c.target, c.guarantor) + BILL::Get_Their_Spy_Number(c.target, c.protege));
        if (spies <= 0 || roll >= spies * SPY_PER_MILLE) return false;
        Reveal(index, "its spies");
        return true;
    }

    bool Can_Marry(int16_t a, int16_t b) {
        if (!marriages || a == b || !Computer_(a) || !Computer_(b)) return false;
        if (State()->spouse[a] >= 0 || State()->spouse[b] >= 0) return false;
        if (!In_Contact_(a, b) || At_War_(a, b) || At_War_(b, a)) return false;
        const s_player& pa = MOX::player[a];
        const s_player& pb = MOX::player[b];
        const bool treaty = (pa.treaty[b] >= 1 && pa.treaty[b] < TREATY_FIRST_WAR && pb.treaty[a] >= 1 && pb.treaty[a] < TREATY_FIRST_WAR)
            || pa.trade_treaty[b] != 0 || pb.trade_treaty[a] != 0;
        if (!treaty) return false;
        return opinion::Relations(a, b) >= MARRY_FRIENDSHIP && opinion::Relations(b, a) >= MARRY_FRIENDSHIP;
    }

    bool Marry(int16_t a, int16_t b) {
        if (!marriages || a == b || !Computer_(a) || !Computer_(b)) return false;
        s_state* s = State();
        if (s->spouse[a] >= 0 || s->spouse[b] >= 0) return false;
        s->spouse[a] = static_cast<int8_t>(b);
        s->spouse[b] = static_cast<int8_t>(a);
        s->married_since[a] = s->married_since[b] = MOX::stardate;
        opinion::Add(a, b, opinion::REASON_FAMILY_TIES, FAMILY_OPINION);
        opinion::Add(b, a, opinion::REASON_FAMILY_TIES, FAMILY_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, players %d and %d marry",
            (int)MOX::stardate, (int)a, (int)b);
        return true;
    }

    void Divorce(int16_t a, int16_t b, int16_t breaker, const char* why) {
        if (a < 0 || a >= MAX_PLAYERS || b < 0 || b >= MAX_PLAYERS) return;
        s_state* s = State();
        if (s->spouse[a] != b) return;
        s->spouse[a] = s->spouse[b] = -1;
        s->married_since[a] = s->married_since[b] = 0;
        opinion::Set(a, b, opinion::REASON_FAMILY_TIES, 0);
        opinion::Set(b, a, opinion::REASON_FAMILY_TIES, 0);
        if (breaker == a || breaker == b) {
            const int16_t wronged = breaker == a ? b : a;
            opinion::Add(wronged, breaker, opinion::REASON_BROKE_ITS_WORD, BROKEN_OPINION);
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == breaker || o == wronged || !Computer_(o) || MOX::player[o].contact[breaker] == 0) continue;
                opinion::Add(o, breaker, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
            }
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, the marriage of players %d and %d ends (breaker %d): %s",
            (int)MOX::stardate, (int)a, (int)b, (int)breaker, why != nullptr ? why : "");
    }

    void On_Aggression(int16_t aggressor, int16_t victim) {
        if (answering || !Alive_(aggressor) || !Alive_(victim) || aggressor == victim) return;
        if (marriages && State()->spouse[aggressor] == victim) Divorce(aggressor, victim, aggressor, "it attacked its spouse");
        if (!secret_clauses) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            const s_clause c = s->clauses[i];
            if (c.protege != victim || c.target != aggressor || Expired_(c) || !Alive_(c.guarantor)) {
                ++i;
                continue;
            }
            const s_player& pg = MOX::player[c.guarantor];
            if (At_War_(c.guarantor, aggressor) || pg.contact[aggressor] == 0) {  // already fighting, or no way to
                ++i;
                continue;
            }
            if (pg.treaty[aggressor] == TREATY_ALLIANCE || diplomemory::Too_Weary_For_New_War(c.guarantor)) {
                opinion::Add(victim, c.guarantor, opinion::REASON_BETRAYED_A_SECRET_PACT, BETRAYED_OPINION);
                End_Clause(i, "the guarantor did not honour it");
                continue;
            }
            answering = true;
            diplomac::Declare_War(c.guarantor, aggressor);
            answering = false;
            opinion::Add(victim, c.guarantor, opinion::REASON_DEFENDED_US, DEFENDED_OPINION);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "covenants: stardate %d, player %d honours its secret clause to player %d: war on player %d",
                (int)MOX::stardate, (int)c.guarantor, (int)victim, (int)aggressor);
            Reveal(i, "called on");
            ++i;
        }
    }

    void On_Treaty_Broken(int16_t breaker, int16_t victim) {
        if (!marriages || !Alive_(breaker) || !Alive_(victim) || breaker == victim) return;
        if (State()->spouse[breaker] == victim) Divorce(breaker, victim, breaker, "it broke a treaty with its spouse");
    }

    void Turn() {
        if (secret_clauses) {
            s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->clauses[i].revealed != 0 || !Alive_(s->clauses[i].target)) continue;
                const int32_t spies = BILL::Get_Their_Spy_Number(s->clauses[i].target, s->clauses[i].guarantor)
                    + BILL::Get_Their_Spy_Number(s->clauses[i].target, s->clauses[i].protege);
                if (spies <= 0) continue;
                Spy_Reveal_With_Roll(i, static_cast<int32_t>(game_random::Random(1000)));
            }
        }
        if (MOX::stardate % PERIOD != PHASE) return;
        if (secret_clauses) {
            Review_Clauses_();
            Sign_Clauses_();
        }
        if (marriages) {
            Keep_Marriages_();
            Make_Marriages_();
        }
    }

} // namespace covenants
