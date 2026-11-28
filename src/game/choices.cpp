#include "pch.h"

#include "game/choices.h"

#include "game/incidents.h"

#include <algorithm>

// The behaviour of the pending choices of a human (docs/LocalLLM/human-ui.md section 5); the queue and the
// save section are in choices_state.cpp. Nothing here draws from game_random.
namespace choices {

    namespace {
        bool Network_Game_() {
            return MOX::game_type == 2 || MOX::game_type == 3 || NETMOX::net.is_network_active != 0;
        }

        // What the window shows for a choice; false when the subject is gone.
        bool Text_(const s_choice& c, s_text* text) {
            switch (c.module) {
                case MODULE_INCIDENTS: return incidents::Choice_Text(c, text);
                default: return false;
            }
        }

        void Apply_(const s_choice& c, int32_t option) {
            switch (c.module) {
                case MODULE_INCIDENTS: incidents::Choice_Apply(c, option); break;
                default: break;
            }
        }

        int32_t First_(int16_t player) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->choice[i].player == player) return i;
            }
            return -1;
        }

        void Remove_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->choice[i] = s->choice[i + 1];
            s->choice[--s->count] = s_choice{};
        }

        void Log_(const s_choice& c, const char* what, int32_t option = -1) {
            if (option >= 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "choices: stardate %d, player %d: module %d kind %d %s %d", (int)MOX::stardate,
                    (int)c.player, (int)c.module, (int)c.kind, what, (int)option);
            } else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "choices: stardate %d, player %d: module %d kind %d %s", (int)MOX::stardate,
                    (int)c.player, (int)c.module, (int)c.kind, what);
            }
        }
    } // namespace

    bool Ask_Human(int16_t player) {
        if (!enabled || player < 0 || player >= MAX_PLAYERS || player >= MOX::NUM_PLAYERS) return false;
        const s_player& p = MOX::player[player];
        return p.eliminated == 0 && p.objectives == PLAYER_OBJECTIVE_HUMAN && !Network_Game_();
    }

    bool Add(const s_choice& choice) {
        s_state* s = State();
        if (s->count >= MAX_PENDING) return false;
        s->choice[s->count++] = choice;
        Log_(choice, "waits for the human");
        return true;
    }

    bool Apply_Pending(int16_t player, int32_t option) {
        const int32_t index = First_(player);
        if (index < 0) return false;
        const s_choice c = State()->choice[index];
        Remove_(index);
        option = std::clamp<int32_t>(option, 0, 2);
        Log_(c, "chose option", option);
        Apply_(c, option);
        return true;
    }

    int8_t Has_Pending(int16_t player, bool show_ui) {
        if (!enabled || Empty()) return 0;
        s_text text;
        for (int32_t i = 0; i < State()->count;) {
            const s_choice& c = State()->choice[i];
            if (c.player == player && !Text_(c, &text)) {
                Log_(c, "dropped, the subject is gone");
                Remove_(i);
            } else {
                ++i;
            }
        }
        const int32_t index = First_(player);
        if (index < 0) return 0;
        if (!show_ui) return 1;
        text = s_text{};
        if (!Text_(State()->choice[index], &text) || text.count < 2) return 0;
        const int16_t picked = GENDRAW::Choice_Box(text);
        Apply_Pending(player, std::clamp<int32_t>(picked, 0, text.count - 1));
        return 1;
    }

} // namespace choices
