#include "pch.h"

#include "game/counterspy.h"

#include "game/bill.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/secrets.h"
#include "game/spy_ranks.h"

#include <algorithm>

// The behaviour of counter-intelligence (mechanics.md 3.5.5); the state and its save section are in
// counterspy_state.cpp. game_random is drawn only by the parts that are on: the hunt's dice check and
// the interrogation's roll.
namespace counterspy {

    namespace {
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;
        constexpr int8_t STATUS_LIMBO = 2;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool Holds_(const s_leader_data& leader, int16_t player) {
            return leader.player_index == player &&
                (leader.status == STATUS_POOL || leader.status == STATUS_ASSIGNED || leader.status == STATUS_LIMBO);
        }

        int32_t Loyalty_Or_Default_(int16_t leader) {
            const int32_t value = loyalty::Of(leader);
            return value == loyalty::UNTRACKED ? DEFAULT_LOYALTY : value;
        }

        bool Has_Skill_(int16_t player, int32_t mask) {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (Holds_(l, player) && (l.general_skills & mask) != 0) return true;
            }
            return false;
        }

        // The bribed leader is uncovered: the bribe ends, the leader is dismissed, the briber remembered.
        void Uncover_(int16_t owner, int16_t leader, const char* how) {
            const int16_t briber = spy_ops::Bribed_By(leader);
            if (briber < 0) return;
            *spy_ops::Bribed_Slot(leader) = -1;
            OFFICER::Deassign_Officer(leader);
            opinion::Add(owner, briber, opinion::REASON_BRIBED_OUR_LEADER, UNCOVERED_OPINION);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d uncovers its leader %d bribed by player %d (%s)",
                (int)MOX::stardate, (int)owner, (int)leader, (int)briber, how);
        }

        void Add_Loyalty_(int16_t leader, int32_t delta) {
            if (loyalty::Of(leader) == loyalty::UNTRACKED) return;
            loyalty::s_record* r = loyalty::Record(leader);
            if (r == nullptr) return;
            r->loyalty = static_cast<int8_t>(std::clamp<int32_t>(r->loyalty + delta, 0, 100));
        }

        bool Kill_Op_(spy_ops::e_op op) {
            return op == spy_ops::OP_ASSASSINATION || op == spy_ops::OP_POISON_FEAST || op == spy_ops::OP_KILL_SCIENTIST;
        }

        bool Key_Leader_(int16_t player) {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                if (Holds_(MOX::leaders[i], player) && OFFICER::Owned_Officer_Level(i) >= GUARD_LEVEL) return true;
            }
            return false;
        }

        bool Wants_Guards_(int16_t player) {
            const int32_t last = State()->attempt[player];
            if (last != 0 && MOX::stardate - last <= GUARD_THREAT_TURNS) return true;
            return MOX::player[player].bc >= GUARD_RICH_BC && Key_Leader_(player);
        }

        void Guards_Turn_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Computer_(p)) {
                    State()->guarded[p] = 0;
                    continue;
                }
                const bool was = State()->guarded[p] != 0;
                bool now = false;
                if (Wants_Guards_(p) && MOX::player[p].bc >= guard_fee) {
                    MOX::player[p].bc -= guard_fee;
                    now = true;
                }
                State()->guarded[p] = now ? 1 : 0;
                if (now != was) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d %s its leaders",
                        (int)MOX::stardate, (int)p, now ? "guards" : "stops guarding");
                }
            }
        }

        // The plan `owner` has against a third empire (or against `victim`): the attacked empire, -1 if none.
        int16_t Planned_Against_(int16_t owner) {
            const s_player& p = MOX::player[owner];
            if (p.sneak_attack_planet == -1) return -1;
            return Alive_(p.sneak_attack_player) && p.sneak_attack_player != owner ? p.sneak_attack_player : -1;
        }
    } // namespace

    bool Suspects(int16_t player) {
        if (!mole_hunt || !Computer_(player)) return false;
        const int32_t since = State()->suspicion[player];
        if (since != 0 && MOX::stardate - since <= SUSPECT_TURNS) return true;
        return espionage::Mode(player) != espionage::MODE_NORMAL;
    }

    int16_t Suspect_Leader(int16_t player) {
        int16_t best = -1;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& l = MOX::leaders[i];
            if (!Holds_(l, player) || l.status != STATUS_ASSIGNED) continue;
            if (best < 0 || Loyalty_Or_Default_(i) < Loyalty_Or_Default_(best)) best = i;
        }
        return best;
    }

    e_hunt Hunt(int16_t hunter) {
        return Hunt_With_Dice(hunter, nullptr);
    }

    e_hunt Hunt_With_Dice(int16_t hunter, const int32_t* dice_values) {
        if (!mole_hunt || !Computer_(hunter)) return HUNT_NONE;
        const int16_t suspect = Suspect_Leader(hunter);
        if (suspect < 0) return HUNT_NONE;
        int32_t agents = 0;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            if (MOX::colony[c].owner == hunter) agents += spy_ranks::Counter_Agents(c);
        }
        const dice::s_modifier modifiers[3] = {
            {"spy master", Has_Skill_(hunter, LEADER_GENERAL_SKILL_SPY_MASTER | LEADER_GENERAL_SKILL_SPY_MASTER2) ? HUNT_SKILL_BONUS : 0},
            {"telepath", Has_Skill_(hunter, LEADER_GENERAL_SKILL_TELEPATH | LEADER_GENERAL_SKILL_TELEPATH2) ? HUNT_SKILL_BONUS : 0},
            {"counter-agents", std::min(HUNT_AGENTS_MAX, agents / 2)}};
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, HUNT_DIFFICULTY)
            : dice::Roll_Check(modifiers, 3, HUNT_DIFFICULTY);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d hunts a mole, suspects leader %d: %s",
            (int)MOX::stardate, (int)hunter, (int)suspect, dice::Describe_Check("mole hunt", check, modifiers, 3).c_str());
        State()->suspicion[hunter] = 0;  // the hunt is held; a new suspicion starts a new one
        const bool success = check.outcome >= dice::OUTCOME_SUCCESS;
        if (spy_ops::Bribed_By(suspect) >= 0) {
            if (!success) return HUNT_NOTHING;
            Uncover_(hunter, suspect, "mole hunt");
            return HUNT_UNCOVERED;
        }
        if (success || check.outcome > dice::OUTCOME_FAILURE) return HUNT_NOTHING;
        // An innocent is jailed; the others are shaken.
        OFFICER::Deassign_Officer(suspect);
        Add_Loyalty_(suspect, JAILED_LOYALTY);
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            if (i != suspect && Holds_(MOX::leaders[i], hunter)) Add_Loyalty_(i, WITCH_LOYALTY);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d jails its innocent leader %d",
            (int)MOX::stardate, (int)hunter, (int)suspect);
        return HUNT_JAILED;
    }

    void On_Secret_Leaked(int16_t owner) {
        if (!Computer_(owner)) return;
        if (mole_hunt) State()->suspicion[owner] = MOX::stardate;
        if (canary_trap && State()->canary[owner] == 0) {
            State()->canary[owner] = MOX::stardate + CANARY_TURNS;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d sets a canary trap",
                (int)MOX::stardate, (int)owner);
        }
    }

    int32_t Canary_Turn(int16_t owner) {
        return owner >= 0 && owner < MAX_PLAYERS ? State()->canary[owner] : 0;
    }

    bool Spring_Canary(int16_t owner) {
        if (!canary_trap || !Alive_(owner)) return false;
        State()->canary[owner] = 0;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            if (!Holds_(MOX::leaders[i], owner)) continue;
            const int16_t briber = spy_ops::Bribed_By(i);
            if (briber < 0 || !Alive_(briber)) continue;
            if (BILL::Get_Their_Spy_Number(briber, owner) == 0 && spy_ranks::Residents(briber, owner) == 0) continue;
            Uncover_(owner, i, "canary trap");
            return true;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d's canary trap catches no one",
            (int)MOX::stardate, (int)owner);
        return false;
    }

    void On_Spy_Caught(int16_t victim, int16_t owner) {
        if (!mole_hunt && !interrogation) return;
        if (!Computer_(victim) || !Alive_(owner) || victim == owner) return;
        if (mole_hunt) State()->suspicion[victim] = MOX::stardate;
        if (!interrogation || Planned_Against_(owner) < 0) return;
        On_Spy_Caught_With_Roll(victim, owner, static_cast<int32_t>(game_random::Random(100)));
    }

    bool On_Spy_Caught_With_Roll(int16_t victim, int16_t owner, int32_t roll) {
        if (!interrogation || !Computer_(victim) || !Alive_(owner) || victim == owner) return false;
        if (roll >= INTERROGATION_PERCENT) return false;
        const int16_t third = Planned_Against_(owner);
        if (third < 0) return false;
        if (third != victim) {
            if (!secrets::Learn(victim, secrets::KIND_PLAN, owner, third, -1)) return false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d interrogates a spy of player %d and learns its plan against player %d",
                (int)MOX::stardate, (int)victim, (int)owner, (int)third);
            return true;
        }
        // The plan is against the interrogator: it is warned, as by its own spies (intrigue.cpp).
        s_player& p = MOX::player[owner];
        opinion::Add(victim, owner, opinion::REASON_PLOTTED_AGAINST_US, intrigue::PLOTTED_OPINION);
        p.sneak_attack_planet = -1;
        p.sneak_attack_player = -1;
        p.war_buildup_duration = 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d interrogates a spy of player %d and learns its plan to attack it; the surprise is lost",
            (int)MOX::stardate, (int)victim, (int)owner);
        return true;
    }

    int16_t Blamed(int16_t target, int16_t attacker, int16_t framed) {
        if (!interrogation || !Computer_(target) || !Alive_(framed) || framed == attacker || framed == target) return attacker;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "counterspy: stardate %d, player %d's interrogated spy names player %d instead of player %d",
            (int)MOX::stardate, (int)target, (int)framed, (int)attacker);
        return framed;
    }

    void On_Attempt(int16_t target, spy_ops::e_op op) {
        if (!guards || !Kill_Op_(op) || !Computer_(target)) return;
        State()->attempt[target] = MOX::stardate;
    }

    bool Guarded(int16_t player) {
        return guards && player >= 0 && player < MAX_PLAYERS && State()->guarded[player] != 0;
    }

    int32_t Guard_Difficulty(int16_t target, spy_ops::e_op op) {
        return Kill_Op_(op) && Guarded(target) ? GUARD_DIFFICULTY : 0;
    }

    void Turn() {
        if (guards) Guards_Turn_();
        if (canary_trap) {
            for (int16_t p = 0; p < Players_(); ++p) {
                const int32_t turn = State()->canary[p];
                if (turn != 0 && MOX::stardate >= turn) Spring_Canary(p);
            }
        }
        if (mole_hunt && MOX::stardate % PERIOD == PHASE) {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (Suspects(p)) Hunt(p);
            }
        }
    }

} // namespace counterspy
