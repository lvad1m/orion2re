#include "pch.h"

#include "game/pacts.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/beliefs.h"
#include "game/choices.h"
#include "game/covenants.h"
#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/intrigue.h"
#include "game/vassals.h"
#include "game/opinion.h"
#include "game/reputation.h"
#include "game/texts.h"

#include <algorithm>

// The behaviour of defence pacts and guarantees: answering an attack and the review. The state and
// its save section are in pacts_state.cpp. Nothing here draws from game_random (Declare_War does,
// as it always has).
namespace pacts {

    namespace {
        // s_player::treaty values (advisor_view.cpp Treaty_Name).
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;

        bool responding = false;  // inside On_Aggression: defensive declarations call on no pacts

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Alive_(int16_t player) {
            return Valid_(player) && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool At_War_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] >= TREATY_FIRST_WAR;
        }

        int32_t Strength_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        // An advised empire signs nothing with an empire it is wary of or worse.
        bool Advisor_Allows_(int16_t player, int16_t partner) {
            if (!advisor::enabled || ((advisor::active_players >> player) & 1) == 0) return true;
            const advisor::s_directive* directive = advisor::Get_Directive(static_cast<uint8_t>(player));
            if (directive == nullptr || !advisor::Is_Active(directive, MOX::stardate)) return true;
            const uint8_t attitude = directive->attitude[partner];
            return attitude == advisor::ATTITUDE_KEEP || attitude < advisor::ATTITUDE_WARY;
        }

        // An empire both `a` and `b` think ill of, with a fleet a match for each of them; -1 if none.
        // `b_human`: b is a human, whose opinion of the threat is its own answer to give (not checked).
        int16_t Common_Threat_(int16_t a, int16_t b, bool b_human = false) {
            for (int16_t t = 0; t < MOX::NUM_PLAYERS && t < MAX_PLAYERS; ++t) {
                if (t == a || t == b || !Alive_(t)) continue;
                if (MOX::player[a].contact[t] == 0 || MOX::player[b].contact[t] == 0) continue;
                if (opinion::Relations(a, t) >= 0 || (!b_human && opinion::Relations(b, t) >= 0)) continue;
                if (Strength_(t, a) >= Strength_(a, t) && Strength_(t, b) >= Strength_(b, t)) return t;
            }
            return -1;
        }

        // An empire `guarantor` thinks ill of that threatens `protege`; -1 if none.
        int16_t Threat_To_(int16_t guarantor, int16_t protege) {
            for (int16_t t = 0; t < MOX::NUM_PLAYERS && t < MAX_PLAYERS; ++t) {
                if (t == guarantor || t == protege || !Alive_(t)) continue;
                if (MOX::player[guarantor].contact[t] == 0 || MOX::player[protege].contact[t] == 0) continue;
                if (opinion::Relations(guarantor, t) >= 0) continue;
                if (Strength_(t, protege) >= Strength_(protege, t)) return t;
            }
            return -1;
        }

        enum e_offer { OFFER_QUEUED, OFFER_NOT_REPEATED, OFFER_NO_ROOM };

        // A computer's offer to a human is queued once; one refused lately is not repeated; a full queue: no offer.
        e_offer Offer_(int16_t human, int16_t computer, uint8_t kind, int16_t threat) {
            const int32_t refused = Last_Refusal(human, computer, kind);
            if (refused >= 0 && MOX::stardate - refused >= 0 && MOX::stardate - refused < REOFFER_TURNS) return OFFER_NOT_REPEATED;
            const choices::s_state* queue = choices::State();
            for (int32_t i = 0; i < queue->count; ++i) {
                const choices::s_choice& q = queue->choice[i];
                if (q.module == choices::MODULE_PACTS && q.kind == kind && q.player == human && q.target == computer) return OFFER_NOT_REPEATED;
            }
            choices::s_choice c;
            c.module = choices::MODULE_PACTS;
            c.kind = kind;
            c.player = static_cast<int8_t>(human);
            c.target = computer;
            c.other = threat;
            c.asked = MOX::stardate;
            return choices::Add(c) ? OFFER_QUEUED : OFFER_NO_ROOM;
        }

        // Pacts for a human: the computer's side of the rule is checked as for a computer partner, the human's own
        // opinion, beliefs and advisor are replaced by its answer. It touches nothing a computer's pick reads and
        // draws nothing, so the pairs of computers are signed as before. (Guarantees are in Review_: a human ward
        // already stood in that loop, silently; with the setting on it is offered there instead.)
        void Offer_To_Humans_(int16_t players) {
            for (int16_t h = 0; h < players; ++h) {
                if (!choices::Ask_Human(h)) continue;
                for (int16_t c = 0; c < players; ++c) {
                    if (c == h || !Computer_(c) || At_War_(c, h)) continue;
                    if (MOX::player[c].contact[h] == 0 || MOX::player[h].contact[c] == 0) continue;
                    if (opinion::Relations(c, h) < opinion_min || !Advisor_Allows_(c, h)) continue;
                    if (!Has_Pact(c, h) && beliefs::Believes_Faithful(c, h)) {
                        const int16_t threat = Common_Threat_(c, h, true);
                        if (threat >= 0) Offer_(h, c, CHOICE_PACT, threat);
                    }
                }
            }
        }

        void Review_() {
            const int16_t players = static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
            // Ends first: terms that are gone.
            for (int16_t a = 0; a < players; ++a) {
                for (int16_t b = static_cast<int16_t>(a + 1); b < players; ++b) {
                    if (!Has_Pact(a, b)) continue;
                    if (!Alive_(a) || !Alive_(b)) {
                        End_Pact(a, b, "a partner is gone");
                    } else if (At_War_(a, b)) {
                        End_Pact(a, b, "the partners are at war");
                    } else if (opinion::Relations(a, b) < 0 || opinion::Relations(b, a) < 0) {
                        End_Pact(a, b, "the partners fell out");
                    }
                }
            }
            for (int16_t g = 0; g < players; ++g) {
                for (int16_t w = 0; w < players; ++w) {
                    if (!Guarantees(g, w)) continue;
                    if (!Alive_(g) || !Alive_(w)) {
                        End_Guarantee(g, w, "a party is gone");
                    } else if (At_War_(g, w)) {
                        End_Guarantee(g, w, "they are at war");
                    } else if (opinion::Relations(g, w) < 0) {
                        End_Guarantee(g, w, "the guarantor fell out with it");
                    } else if (Strength_(w, g) >= Strength_(g, w)) {
                        End_Guarantee(g, w, "it is no longer the weaker");
                    }
                }
            }
            // New pacts between computer empires facing a common threat.
            for (int16_t a = 0; a < players; ++a) {
                if (!Computer_(a)) continue;
                for (int16_t b = static_cast<int16_t>(a + 1); b < players; ++b) {
                    if (!Computer_(b) || Has_Pact(a, b) || At_War_(a, b)) continue;
                    if (MOX::player[a].contact[b] == 0 || MOX::player[b].contact[a] == 0) continue;
                    if (opinion::Relations(a, b) < opinion_min || opinion::Relations(b, a) < opinion_min) continue;
                    if (!Advisor_Allows_(a, b) || !Advisor_Allows_(b, a)) continue;
                    if (!beliefs::Believes_Faithful(a, b) || !beliefs::Believes_Faithful(b, a)) continue;
                    const int16_t threat = Common_Threat_(a, b);
                    if (threat >= 0) Sign_Pact(a, b, threat);
                }
            }
            // New guarantees: a much stronger empire protects a neighbour it values.
            for (int16_t g = 0; g < players; ++g) {
                if (!Computer_(g)) continue;
                int32_t offered = 0;  // human wards asked instead of guaranteed: they keep their slot, so the others are as before
                for (int16_t w = 0; w < players && Guarantee_Count(g) + offered < guarantees_max; ++w) {
                    if (w == g || !Alive_(w) || Guarantees(g, w) || At_War_(g, w)) continue;
                    const s_player& me = MOX::player[g];
                    if (me.contact[w] != 1 || me.within_range[w] != 1) continue;
                    if (opinion::Relations(g, w) < opinion_min || !Advisor_Allows_(g, w)) continue;
                    if (Strength_(g, w) < 2 * std::max(1, Strength_(w, g))) continue;
                    const int16_t threat = Threat_To_(g, w);
                    if (threat < 0) continue;
                    if (choices::Ask_Human(w) && Offer_(w, g, CHOICE_GUARANTEE, threat) != OFFER_NO_ROOM) {
                        ++offered;  // a full queue: the human gets the guarantee as today
                        continue;
                    }
                    Give_Guarantee(g, w, threat);
                }
            }
            Offer_To_Humans_(players);
        }
    } // namespace

    // The offer still stands: both alive, not at war, the treaty not yet made, the aim alive, and for a guarantee
    // the guarantor has a slot left.
    bool Choice_Text(const choices::s_choice& c, choices::s_text* t) {
        const int16_t human = c.player;
        const int16_t computer = c.target;
        const int16_t threat = c.other;
        if (!enabled || !Alive_(human) || !Computer_(computer) || !Alive_(threat) || human == computer || threat == human || threat == computer) return false;
        if (At_War_(computer, human)) return false;
        if (c.kind == CHOICE_PACT) {
            if (Has_Pact(computer, human)) return false;
        } else if (c.kind == CHOICE_GUARANTEE) {
            if (Guarantees(computer, human) || Guarantee_Count(computer) >= guarantees_max) return false;
        } else {
            return false;
        }
        const auto Short = [](int16_t p) { return std::string(MOX::player[p].race_name, strnlen(MOX::player[p].race_name, 14)); };
        *t = choices::s_text{};
        const char* base = c.kind == CHOICE_PACT ? "choices.pacts.pact." : "choices.pacts.guarantee.";
        const std::initializer_list<std::pair<const char*, std::string>> args = {{"race", Short(computer)}, {"threat", Short(threat)}};
        texts::Format(t->title, sizeof(t->title), (std::string(base) + "title").c_str(), {});
        texts::Format(t->body, sizeof(t->body), (std::string(base) + "body").c_str(), args);
        texts::Format(t->option[0], sizeof(t->option[0]), (std::string(base) + "option1").c_str(), args);
        texts::Format(t->option[1], sizeof(t->option[1]), (std::string(base) + "option2").c_str(), args);
        t->count = 2;
        return true;
    }

    void Choice_Apply(const choices::s_choice& c, int32_t option) {
        choices::s_text unused;
        if (!Choice_Text(c, &unused)) return;
        if (option == 0) {
            if (c.kind == CHOICE_PACT) {
                Sign_Pact(c.target, c.player, c.other);
            } else {
                Give_Guarantee(c.target, c.player, c.other);
            }
        } else {
            Note_Refusal(c.player, c.target, c.kind);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, player %d refuses the %s of player %d", (int)MOX::stardate,
                (int)c.player, c.kind == CHOICE_PACT ? "pact" : "guarantee", (int)c.target);
        }
    }

    void On_Aggression(int16_t aggressor, int16_t victim) {
        vassals::On_Aggression(aggressor, victim);  // an overlord defends its vassal (off by default)
        covenants::On_Aggression(aggressor, victim);  // secret clauses answer, a marriage breaks (off by default)
        reputation::On_Aggression(aggressor, victim);  // an attack during talks or a wedding is a red wedding (off by default)
        if (!enabled || responding || !Alive_(aggressor) || !Alive_(victim) || aggressor == victim) return;
        responding = true;
        for (int16_t p = 0; p < MOX::NUM_PLAYERS && p < MAX_PLAYERS; ++p) {
            if (p == aggressor || p == victim || !Computer_(p)) continue;
            if (!Has_Pact(p, victim) && !Guarantees(p, victim)) continue;
            const s_player& partner = MOX::player[p];
            if (partner.contact[aggressor] == 0 || At_War_(p, aggressor) || partner.treaty[aggressor] == TREATY_ALLIANCE) continue;
            if (diplomemory::Too_Weary_For_New_War(p)) {
                opinion::Add(victim, p, opinion::REASON_ABANDONED_US, abandoned_opinion);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, player %d abandons player %d to player %d",
                    (int)MOX::stardate, (int)p, (int)victim, (int)aggressor);
                End_Pact(p, victim, "the partner stayed out of the war");
                End_Guarantee(p, victim, "the guarantor stayed out of the war");
                continue;
            }
            diplomac::Declare_War(p, aggressor);
            opinion::Add(victim, p, opinion::REASON_DEFENDED_US, defended_opinion);
            intrigue::Owe(victim, p);  // favours (off by default)
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, player %d honours its word to player %d: war on player %d",
                (int)MOX::stardate, (int)p, (int)victim, (int)aggressor);
        }
        responding = false;
    }

    void End_Of_Turn() {
        if (!enabled || period < 1 || MOX::stardate % period != 0) return;
        Review_();
    }

} // namespace pacts
