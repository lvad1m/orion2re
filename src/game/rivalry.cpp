#include "pch.h"

#include "game/rivalry.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <algorithm>

// Nothing in this file draws from game_random.
namespace rivalry {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_PLAYERS * (sizeof(int8_t) + sizeof(int32_t));
        // s_player::treaty values (advisor_view.cpp Treaty_Name).
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_PEACE = 3;

        int8_t rival[MAX_PLAYERS] = {-1, -1, -1, -1, -1, -1, -1, -1};
        int32_t since[MAX_PLAYERS] = {};
        int8_t pending_rival[MAX_PLAYERS];
        int32_t pending_since[MAX_PLAYERS];
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Alive_(int16_t player) {
            return Valid_(player) && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool Friends_(int16_t a, int16_t b) {
            const int8_t treaty = MOX::player[a].treaty[b];
            return treaty == TREATY_ALLIANCE || treaty == TREATY_PEACE;
        }

        // The active advisor directive of `player`, or nullptr.
        const advisor::s_directive* Directive_(int16_t player) {
            if (!advisor::enabled || ((advisor::active_players >> player) & 1) == 0) return nullptr;
            const advisor::s_directive* directive = advisor::Get_Directive(static_cast<uint8_t>(player));
            return directive != nullptr && advisor::Is_Active(directive, MOX::stardate) ? directive : nullptr;
        }

        bool Should_Drop_(int16_t player, int16_t current) {
            if (!Alive_(current) || Friends_(player, current)) return true;
            if (opinion::Relations(player, current) > 0) return true;
            const advisor::s_directive* directive = Directive_(player);
            return directive != nullptr && directive->main_rival < MAX_PLAYERS && directive->main_rival != current;
        }

        int16_t Candidate_(int16_t player) {
            const s_player& me = MOX::player[player];
            if (const advisor::s_directive* directive = Directive_(player)) {
                const int16_t m = directive->main_rival < MAX_PLAYERS ? directive->main_rival : NO_RIVAL;
                if (m != NO_RIVAL && m != player && Alive_(m) && me.contact[m] != 0 && !Friends_(player, m) &&
                    directive->attitude[m] >= advisor::ATTITUDE_HOSTILE) {
                    return m;
                }
                return NO_RIVAL;
            }
            int16_t worst = NO_RIVAL;
            int32_t worst_opinion = 0;
            for (int16_t j = 0; j < MOX::NUM_PLAYERS && j < MAX_PLAYERS; ++j) {
                if (j == player || !Alive_(j) || me.contact[j] != 1 || me.within_range[j] != 1 || Friends_(player, j)) continue;
                const int32_t value = opinion::Relations(player, j);
                if (worst == NO_RIVAL || value < worst_opinion) {
                    worst = j;
                    worst_opinion = value;
                }
            }
            return worst != NO_RIVAL && worst_opinion <= hatred ? worst : NO_RIVAL;
        }
    } // namespace

    int16_t Rival_Of(int16_t player) {
        if (!enabled || player < 0 || player >= MAX_PLAYERS) return NO_RIVAL;
        return rival[player];
    }

    int32_t Rival_Since(int16_t player) {
        if (player < 0 || player >= MAX_PLAYERS || rival[player] == NO_RIVAL) return 0;
        return since[player];
    }

    void Declare(int16_t player, int16_t target) {
        if (!enabled || !Alive_(player) || !Alive_(target) || player == target || rival[player] == target) return;
        rival[player] = static_cast<int8_t>(target);
        since[player] = MOX::stardate;
        opinion::Set(target, player, opinion::REASON_RIVAL, opinion_value);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "rivalry: stardate %d, player %d declares player %d its rival",
            (int)MOX::stardate, (int)player, (int)target);
    }

    void Drop(int16_t player) {
        if (player < 0 || player >= MAX_PLAYERS || rival[player] == NO_RIVAL) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "rivalry: stardate %d, player %d drops its rivalry with player %d",
            (int)MOX::stardate, (int)player, (int)rival[player]);
        rival[player] = NO_RIVAL;
        since[player] = 0;
    }

    int32_t Strength_Percent(int16_t player, int16_t target) {
        return Rival_Of(player) == target && target != NO_RIVAL ? strength_percent : 100;
    }

    void End_Of_Turn() {
        if (!enabled) return;
        // The rival's memory of the declaration holds while the rivalry does.
        for (int16_t p = 0; p < MOX::NUM_PLAYERS && p < MAX_PLAYERS; ++p) {
            if (rival[p] != NO_RIVAL) opinion::Set(rival[p], p, opinion::REASON_RIVAL, opinion_value);
        }
        if (period < 1 || MOX::stardate % period != 0) return;
        for (int16_t p = 0; p < MOX::NUM_PLAYERS && p < MAX_PLAYERS; ++p) {
            if (!Computer_(p)) continue;
            if (rival[p] != NO_RIVAL && Should_Drop_(p, rival[p])) Drop(p);
            if (rival[p] == NO_RIVAL) {
                const int16_t candidate = Candidate_(p);
                if (candidate != NO_RIVAL) Declare(p, candidate);
            }
        }
    }

    void Clear() {
        std::fill(std::begin(rival), std::end(rival), static_cast<int8_t>(NO_RIVAL));
        std::fill(std::begin(since), std::end(since), 0);
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(rival), std::end(rival), [](int8_t r) { return r == NO_RIVAL; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            writer->Write_Int8(rival[i]);
            writer->Write_Int32(since[i]);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("rivalry save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported rivalry payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("rivalry save has the wrong size");
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            pending_rival[i] = reader->Read_Int8();
            pending_since[i] = reader->Read_Int32();
            if (pending_rival[i] < NO_RIVAL || pending_rival[i] >= MAX_PLAYERS || pending_rival[i] == i ||
                pending_since[i] < 0) {
                return fail("rivalry save has an out-of-range value");
            }
        }
        if (reader->Failed()) return fail("rivalry save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending_rival), std::end(pending_rival), std::begin(rival));
        std::copy(std::begin(pending_since), std::end(pending_since), std::begin(since));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace rivalry
