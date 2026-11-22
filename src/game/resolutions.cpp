#include "pch.h"

#include "game/resolutions.h"

#include "game/council.h"
#include "game/embargo.h"
#include "game/opinion.h"

#include <algorithm>
#include <cstdio>

// The behaviour of the bans and the Antaran defence fund, and the council's choice among
// resolutions; the state and its save section are in resolutions_state.cpp. Nothing here draws
// from game_random.
namespace resolutions {

    namespace {
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

        int32_t Votes_(int16_t player) {
            return Alive_(player) ? council::Get_Num_Votes(static_cast<uint8_t>(player)) : 0;
        }

        const char* Ban_Name_(e_ban ban) {
            return ban == BAN_BIO_WEAPONS ? "a ban on biological weapons" : "a ban on destroying planets";
        }

        // The alive empire with the most council votes, other than `except`.
        int16_t Largest_(int16_t except) {
            int16_t best = -1;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == except || !Alive_(k)) continue;
                if (best < 0 || Votes_(k) > Votes_(best)) best = k;
            }
            return best;
        }

        enum e_kind : uint8_t { KIND_NONE, KIND_EMBARGO, KIND_BAN, KIND_FUND };

        struct s_proposal {
            e_kind kind = KIND_NONE;
            int16_t proposer = -1;
            int16_t target = -1;  // the embargo's target
            e_ban ban = BAN_BIO_WEAPONS;
            int32_t support = -1;
        };

        void Consider_(s_proposal* best, const s_proposal& candidate) {
            if (candidate.kind != KIND_NONE && candidate.proposer >= 0 && candidate.support > best->support) *best = candidate;
        }
    } // namespace

    void On_Banned_Act(int16_t player, e_ban ban) {
        if (!Active() || !Valid_(player) || ban >= BAN_COUNT) return;
        s_state* s = State();
        const bool first_this_turn = s->last_use[ban][player] != MOX::stardate;
        s->last_use[ban][player] = MOX::stardate;
        if (!Ban_In_Force(ban) || !first_this_turn) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "resolutions: stardate %d, player %d breaks %s",
            (int)MOX::stardate, (int)player, Ban_Name_(ban));
        for (int16_t k = 0; k < Players_(); ++k) {
            if (k != player) opinion::Add(k, player, opinion::REASON_BROKE_BAN, violation_opinion);
        }
        const int16_t initiator = Largest_(player);
        if (initiator >= 0) embargo::Declare(initiator, player, true);
    }

    void On_Antaran_Attack(int16_t victim) {
        if (!Active()) return;
        s_state* s = State();
        s->last_antaran_attack = MOX::stardate;
        if (s->fund == 0 || !Alive_(victim)) return;
        const int32_t payout = s->fund_pool / 2;
        if (payout <= 0) return;
        s->fund_pool -= payout;
        MOX::player[victim].bc += payout;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "resolutions: stardate %d, the Antaran defence fund pays player %d %d BC",
            (int)MOX::stardate, (int)victim, (int)payout);
    }

    void End_Of_Turn() {
        if (!Active()) return;
        s_state* s = State();
        for (uint8_t b = 0; b < BAN_COUNT; ++b) {
            if (((s->bans >> b) & 1) != 0 && MOX::stardate - s->ban_start[b] >= turns) {
                s->bans = static_cast<uint8_t>(s->bans & ~(1u << b));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "resolutions: stardate %d, %s ends", (int)MOX::stardate,
                    Ban_Name_(static_cast<e_ban>(b)));
            }
        }
        if (s->fund == 0) return;
        if (MOX::stardate - s->fund_start >= turns) {
            s->fund = 0;
            s->fund_pool = 0;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "resolutions: stardate %d, the Antaran defence fund ends", (int)MOX::stardate);
            return;
        }
        for (int16_t k = 0; k < Players_(); ++k) {
            if (!Computer_(k)) continue;  // the human pays with the council screen
            s_player& member = MOX::player[k];
            const int32_t payment = std::min<int32_t>(member.bc, std::max<int32_t>(0, member.surplus_bc) * fund_percent / 100);
            if (payment <= 0) continue;
            member.bc -= payment;
            s->fund_pool += payment;
        }
    }

    void Council_Meeting() {
        if (!Active()) return;
        s_state* s = State();
        s_proposal best;
        {
            s_proposal p;
            p.kind = KIND_EMBARGO;
            if (embargo::Council_Proposal(&p.proposer, &p.target, &p.support)) Consider_(&best, p);
        }
        for (uint8_t b = 0; b < BAN_COUNT; ++b) {
            if (((s->bans >> b) & 1) != 0) continue;
            bool used_since = false;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (s->last_use[b][k] != 0 && s->last_use[b][k] >= s->last_meeting) used_since = true;
            }
            if (!used_since) continue;
            s_proposal p;
            p.kind = KIND_BAN;
            p.ban = static_cast<e_ban>(b);
            p.support = 0;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (!Alive_(k) || s->last_use[b][k] != 0) continue;  // those who never did it vote for
                p.support += Votes_(k);
                if (Computer_(k) && (p.proposer < 0 || Votes_(k) > Votes_(p.proposer))) p.proposer = k;
            }
            Consider_(&best, p);
        }
        if (s->fund == 0 && s->last_antaran_attack != 0 && s->last_antaran_attack >= s->last_meeting) {
            int16_t richest = -1;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (Alive_(k) && (richest < 0 || MOX::player[k].surplus_bc > MOX::player[richest].surplus_bc)) richest = k;
            }
            s_proposal p;
            p.kind = KIND_FUND;
            p.support = 0;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (!Alive_(k) || k == richest) continue;  // the largest payer votes against
                p.support += Votes_(k);
                if (Computer_(k) && (p.proposer < 0 || Votes_(k) > Votes_(p.proposer))) p.proposer = k;
            }
            Consider_(&best, p);
        }
        s->last_meeting = MOX::stardate;
        if (best.kind == KIND_NONE) return;
        int32_t total = 0;
        for (int16_t k = 0; k < Players_(); ++k) total += Votes_(k);
        const bool passed = 2 * best.support > total;
        char what[96];
        if (best.kind == KIND_EMBARGO) {
            std::snprintf(what, sizeof(what), "an embargo on player %d", (int)best.target);
        } else if (best.kind == KIND_BAN) {
            std::snprintf(what, sizeof(what), "%s", Ban_Name_(best.ban));
        } else {
            std::snprintf(what, sizeof(what), "an Antaran defence fund");
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "council: stardate %d, resolution: %s, proposed by player %d, %d of %d votes for, %s",
            (int)MOX::stardate, what, (int)best.proposer, (int)best.support, (int)total, passed ? "passed" : "failed");
        if (!passed) return;
        if (best.kind == KIND_EMBARGO) {
            embargo::Council_Enact(best.proposer, best.target);
        } else if (best.kind == KIND_BAN) {
            s->bans = static_cast<uint8_t>(s->bans | (1u << best.ban));
            s->ban_start[best.ban] = MOX::stardate;
        } else {
            s->fund = 1;
            s->fund_start = MOX::stardate;
            s->fund_pool = 0;
        }
    }

} // namespace resolutions
