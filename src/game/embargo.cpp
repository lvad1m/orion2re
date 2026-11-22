#include "pch.h"

#include "game/embargo.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/council.h"
#include "game/opinion.h"

#include <algorithm>

// The behaviour of embargoes and the council's embargo resolution; the state and its save section
// are in embargo_state.cpp. Nothing here draws from game_random.
namespace embargo {

    namespace {
        // s_player::treaty values (advisor_view.cpp Treaty_Name).
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;
        // An opinion this bad makes joining worth losing a trade treaty.
        constexpr int32_t SPITE = -50;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Alive_(int16_t player) {
            return Valid_(player) && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        int Wars_(int16_t target) {
            int wars = 0;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k != target && Alive_(k) && MOX::player[target].treaty[k] >= TREATY_FIRST_WAR) ++wars;
            }
            return wars;
        }

        // An advised empire friendly to (or allied with) `target` takes no part against it.
        bool Advisor_Friendly_(int16_t player, int16_t target) {
            if (!advisor::enabled || ((advisor::active_players >> player) & 1) == 0) return false;
            const advisor::s_directive* directive = advisor::Get_Directive(static_cast<uint8_t>(player));
            if (directive == nullptr || !advisor::Is_Active(directive, MOX::stardate)) return false;
            const uint8_t attitude = directive->attitude[target];
            return attitude == advisor::ATTITUDE_ALLY || attitude == advisor::ATTITUDE_FRIENDLY;
        }

        bool Bad_Name_(int16_t player, int16_t target) {
            return MOX::player[player].broken_treaties_modifier[target] < 0 ||
                opinion::Modifier(player, target, opinion::REASON_SNEAK_ATTACK) < 0 || Wars_(target) >= 3;
        }

        // The embargo ends the treaties; the classic penalty for breaking a treaty does not apply:
        // the target's anger is the opinion modifier.
        void End_Treaties_(int16_t a, int16_t b) {
            MOX::player[a].trade_treaty[b] = 0;
            MOX::player[b].trade_treaty[a] = 0;
            MOX::player[a].research_treaty[b] = 0;
            MOX::player[b].research_treaty[a] = 0;
        }

        // The target `player` would name, `allow_open` admitting a target already under an embargo
        // that is not the council's (the council may make it binding).
        int16_t Candidate_(int16_t player, bool allow_open) {
            if (!Computer_(player)) return -1;
            int16_t worst = -1;
            int32_t worst_opinion = 0;
            for (int16_t t = 0; t < Players_(); ++t) {
                if (t == player || !Alive_(t) || MOX::player[player].contact[t] == 0) continue;
                const s_embargo* e = Record(t);
                if (Active(t) && (!allow_open || e->by_council != 0)) continue;
                if (MOX::player[player].treaty[t] == TREATY_ALLIANCE || Advisor_Friendly_(player, t)) continue;
                const int32_t value = opinion::Relations(player, t);
                if (value > opinion_min || !Bad_Name_(player, t)) continue;
                if (worst < 0 || value < worst_opinion) {
                    worst = t;
                    worst_opinion = value;
                }
            }
            return worst;
        }

        bool Votes_For_(int16_t voter, int16_t target, int16_t proposer) {
            if (voter == target || !Alive_(voter)) return false;
            return voter == proposer || MOX::player[voter].treaty[proposer] == TREATY_ALLIANCE || Would_Join(voter, target);
        }
    } // namespace

    void Declare(int16_t initiator, int16_t target, bool by_council) {
        if (!enabled || (by_council && !council_resolutions) || !Alive_(initiator) || !Alive_(target) || initiator == target) return;
        s_embargo* e = Record(target);
        if (e->participants == 0) {
            *e = s_embargo{};
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k != target && Alive_(k) && MOX::player[target].trade_treaty[k] != 0) {
                    e->former_partners = static_cast<uint8_t>(e->former_partners | (1u << k));
                }
            }
        } else if (!by_council) {
            return;  // one embargo per target
        }
        e->initiator = static_cast<int8_t>(initiator);
        e->start = MOX::stardate;
        e->by_council = by_council ? 1 : 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "embargo: stardate %d, player %d declares an embargo on player %d%s",
            (int)MOX::stardate, (int)initiator, (int)target, by_council ? " by the council's resolution" : "");
        Join(initiator, target);
        if (by_council) {
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k != target) Join(k, target);
            }
        }
    }

    void Join(int16_t member, int16_t target) {
        if (!enabled || !Alive_(member) || !Alive_(target) || member == target) return;
        s_embargo* e = Record(target);
        if (e->initiator < 0 || ((e->participants >> member) & 1) != 0) return;
        e->participants = static_cast<uint8_t>(e->participants | (1u << member));
        End_Treaties_(member, target);
        opinion::Set(target, member, opinion::REASON_EMBARGO, opinion_value);
        if (member != e->initiator) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "embargo: stardate %d, player %d joins the embargo on player %d",
                (int)MOX::stardate, (int)member, (int)target);
        }
    }

    void Leave(int16_t member, int16_t target, const char* why) {
        s_embargo* e = Record(target);
        if (e == nullptr || member < 0 || member >= MAX_PLAYERS || ((e->participants >> member) & 1) == 0) return;
        e->participants = static_cast<uint8_t>(e->participants & ~(1u << member));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "embargo: stardate %d, player %d leaves the embargo on player %d: %s",
            (int)MOX::stardate, (int)member, (int)target, why != nullptr ? why : "");
        if (e->participants == 0) *e = s_embargo{};
    }

    void Lift(int16_t target, const char* why) {
        s_embargo* e = Record(target);
        if (e == nullptr || e->participants == 0) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "embargo: stardate %d, the embargo on player %d is lifted: %s",
            (int)MOX::stardate, (int)target, why != nullptr ? why : "");
        *e = s_embargo{};
    }

    bool Would_Join(int16_t member, int16_t target) {
        if (!Computer_(member) || !Alive_(target) || member == target) return false;
        if (MOX::player[member].treaty[target] == TREATY_ALLIANCE || Advisor_Friendly_(member, target)) return false;
        const int32_t value = opinion::Relations(member, target);
        if (value > join_opinion) return false;
        return MOX::player[member].trade_treaty[target] == 0 || value <= SPITE;
    }

    int16_t Would_Declare(int16_t player) {
        return Candidate_(player, false);
    }

    void End_Of_Turn() {
        if (!enabled) return;
        for (int16_t t = 0; t < Players_(); ++t) {
            s_embargo* e = Record(t);
            if (e->participants == 0) continue;
            if (!Alive_(t)) {
                Lift(t, "the target is gone");
                continue;
            }
            for (int16_t k = 0; k < Players_(); ++k) {
                if (((e->participants >> k) & 1) == 0) continue;
                if (!Alive_(k)) {
                    Leave(k, t, "the member is gone");
                } else if (MOX::player[k].treaty[t] == TREATY_ALLIANCE) {
                    Leave(k, t, "the member is allied with the target");
                } else {
                    opinion::Set(t, k, opinion::REASON_EMBARGO, opinion_value);  // held while the embargo lasts
                }
            }
            if (e->participants == 0) continue;
            if (Coalition_Holds(t)) {
                s_player& target = MOX::player[t];
                const int32_t loss = std::max<int32_t>(0, target.surplus_bc) * penalty_percent / 100;
                if (loss > 0) target.bc = std::max<int32_t>(0, target.bc - loss);
            }
            if (e->by_council != 0 && MOX::stardate - e->start >= council_turns) Lift(t, "the council's term ran out");
        }
        if (period < 1 || MOX::stardate % period != 0) return;
        // The initiators' reviews.
        for (int16_t t = 0; t < Players_(); ++t) {
            const s_embargo* e = Record(t);
            if (e->participants == 0 || e->by_council != 0) continue;
            const int16_t initiator = e->initiator;
            if (!Alive_(initiator)) {
                Lift(t, "the initiator is gone");
            } else if (Computer_(initiator) && MOX::stardate - e->start >= min_turns &&
                       opinion::Relations(initiator, t) > 0 && Wars_(t) < 3) {
                Lift(t, "the initiator lifted it");
            }
        }
        // New embargoes, then who joins.
        for (int16_t p = 0; p < Players_(); ++p) {
            const int16_t t = Would_Declare(p);
            if (t >= 0) Declare(p, t, false);
        }
        for (int16_t t = 0; t < Players_(); ++t) {
            if (!Active(t)) continue;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k != t && !Participates(k, t) && Would_Join(k, t)) Join(k, t);
            }
        }
    }

    bool Council_Proposal(int16_t* proposer, int16_t* target, int32_t* support) {
        if (!enabled || !council_resolutions) return false;
        int16_t best_target = -1;
        int16_t best_proposer = -1;
        int32_t best_support = -1;
        for (int16_t p = 0; p < Players_(); ++p) {
            const int16_t t = Candidate_(p, true);
            if (t < 0) continue;
            int32_t votes_for = 0;
            for (int16_t v = 0; v < Players_(); ++v) {
                if (Votes_For_(v, t, p)) votes_for += council::Get_Num_Votes(static_cast<uint8_t>(v));
            }
            if (votes_for > best_support) {
                best_target = t;
                best_proposer = p;
                best_support = votes_for;
            }
        }
        if (best_target < 0) return false;
        if (proposer != nullptr) *proposer = best_proposer;
        if (target != nullptr) *target = best_target;
        if (support != nullptr) *support = best_support;
        return true;
    }

    void Council_Enact(int16_t proposer, int16_t target) {
        Declare(proposer, target, true);
    }

    void Council_Resolution() {
        int16_t best_target = -1;
        int16_t best_proposer = -1;
        int32_t best_support = -1;
        if (!Council_Proposal(&best_proposer, &best_target, &best_support)) return;
        int32_t total = 0;
        for (int16_t v = 0; v < Players_(); ++v) {
            if (Alive_(v)) total += council::Get_Num_Votes(static_cast<uint8_t>(v));
        }
        const bool passed = 2 * best_support > total;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
            "council: stardate %d, resolution: an embargo on player %d, proposed by player %d, %d of %d votes for, %s",
            (int)MOX::stardate, (int)best_target, (int)best_proposer, (int)best_support, (int)total, passed ? "passed" : "failed");
        if (passed) Council_Enact(best_proposer, best_target);
    }

} // namespace embargo
