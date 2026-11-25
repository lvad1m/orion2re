#include "pch.h"

#include "game/reputation.h"

#include "game/serialization.h"

#include <algorithm>

// The state of stains and titles, what they add to relations, and the save section; the deeds and
// reviews are in reputation.cpp. Kept apart so opinion.cpp and the save code need no diplomacy code.
// Nothing here draws from game_random.
namespace reputation {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;
        constexpr int32_t TITLES_V1 = 8;
        constexpr uint32_t PLAYER_SIZE = TITLE_COUNT * 4 + 2 * 4 + 2 * 4 + 2 + 1;
        constexpr uint32_t TALKS_SIZE = 8 * 8 * 4;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_PLAYERS * PLAYER_SIZE + 4 + TALKS_SIZE;
        constexpr uint32_t PLAYER_SIZE_V1 = TITLES_V1 * 4 + 2 * 4 + 2 * 4 + 2 + 1;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_PLAYERS * PLAYER_SIZE_V1 + 4;

        static_assert(MAX_PLAYERS == 8, "the reputation state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }
    } // namespace

    s_state* State() {
        return &state;
    }

    const char* Title_Name(e_title title) {
        switch (title) {
            case TITLE_OATHBREAKER: return "oathbreaker";
            case TITLE_KINGSLAYER: return "kingslayer";
            case TITLE_ATROCITIES: return "the North remembers";
            case TITLE_KEEPS_WORD: return "keeps its word";
            case TITLE_PEACEMAKER: return "peacemaker";
            case TITLE_TRADE_PRINCE: return "trade prince";
            case TITLE_WARLORD: return "warlord";
            case TITLE_CORSAIR: return "corsair";
            case TITLE_HOSPITALITY_BREAKER: return "breaker of hospitality";
            default: return "none";
        }
    }

    bool Has(int16_t player, e_title title) {
        if (!(enabled || red_wedding) || !Valid_(player) || title >= TITLE_COUNT) return false;
        return state.until[player][title] > MOX::stardate;
    }

    uint32_t Titles(int16_t player) {
        uint32_t bits = 0;
        for (int32_t t = 0; t < TITLE_COUNT; ++t) {
            if (Has(player, static_cast<e_title>(t))) bits |= 1u << t;
        }
        return bits;
    }

    int32_t Relations_Bias(int16_t holder, int16_t target) {
        if (!enabled || !Valid_(holder) || !Valid_(target) || holder == target) return 0;
        const s_player& h = MOX::player[holder];
        if (h.objectives == PLAYER_OBJECTIVE_HUMAN) return 0;
        const bool honorable = h.personality == PLAYER_PERSONALITY_HONORABLE;
        const bool pacifist = h.personality == PLAYER_PERSONALITY_PACIFIST;
        const bool warlike = h.personality == PLAYER_PERSONALITY_AGGRESSIVE || h.personality == PLAYER_PERSONALITY_RUTHLESS;
        int32_t bias = 0;
        if (Has(target, TITLE_OATHBREAKER)) bias -= honorable ? 30 : 20;
        if (Has(target, TITLE_KINGSLAYER)) bias -= 15;
        if (Has(target, TITLE_ATROCITIES)) bias -= honorable || pacifist ? 20 : 10;
        if (Has(target, TITLE_KEEPS_WORD)) bias += honorable ? 15 : 10;
        if (Has(target, TITLE_PEACEMAKER)) bias += pacifist ? 15 : 10;
        if (Has(target, TITLE_TRADE_PRINCE)) bias += 5;
        if (Has(target, TITLE_WARLORD)) bias -= warlike ? 0 : 5;
        if (Has(target, TITLE_CORSAIR)) bias -= 10;
        // The red wedding's stain acts through its own memory (opinion::REASON_VIOLATED_HOSPITALITY), not here.
        return std::clamp<int32_t>(bias, -BIAS_LIMIT, BIAS_LIMIT);
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        const s_state empty{};
        return std::equal(&state.until[0][0], &state.until[0][0] + 8 * TITLE_COUNT, &empty.until[0][0]) &&
            std::equal(&state.breaches[0][0], &state.breaches[0][0] + 16, &empty.breaches[0][0]) &&
            std::equal(&state.peaces[0][0], &state.peaces[0][0] + 16, &empty.peaces[0][0]) &&
            std::all_of(std::begin(state.deeds), std::end(state.deeds), [](int16_t v) { return v == 0; }) &&
            std::all_of(std::begin(state.at_war), std::end(state.at_war), [](uint8_t v) { return v == 0; }) &&
            state.deeds_window == 0 &&
            std::all_of(&state.talks[0][0], &state.talks[0][0] + 64, [](int32_t v) { return v == 0; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            for (int32_t v : state.until[p]) writer->Write_Int32(v);
            for (int32_t v : state.breaches[p]) writer->Write_Int32(v);
            for (int32_t v : state.peaces[p]) writer->Write_Int32(v);
            writer->Write_Int16(state.deeds[p]);
            writer->Write_Uint8(state.at_war[p]);
        }
        writer->Write_Int32(state.deeds_window);
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            for (int32_t v : state.talks[p]) writer->Write_Int32(v);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("reputation save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) return fail("unsupported reputation payload version");
        const uint32_t expected = version == 1 ? SAVE_SIZE_V1 : SAVE_SIZE;
        if (reader->Remaining() != expected - sizeof(uint16_t)) return fail("reputation save has the wrong size");
        const int32_t titles_saved = version == 1 ? TITLES_V1 : TITLE_COUNT;
        pending = s_state{};
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            for (int32_t t = 0; t < titles_saved; ++t) pending.until[p][t] = reader->Read_Int32();
            for (int32_t& v : pending.breaches[p]) v = reader->Read_Int32();
            for (int32_t& v : pending.peaces[p]) v = reader->Read_Int32();
            pending.deeds[p] = reader->Read_Int16();
            pending.at_war[p] = reader->Read_Uint8();
            const bool bad = std::any_of(std::begin(pending.until[p]), std::end(pending.until[p]), [](int32_t v) { return v < 0; }) ||
                pending.deeds[p] < 0;
            if (bad) return fail("reputation save has a bad value");
        }
        pending.deeds_window = reader->Read_Int32();
        if (version >= 2) {
            for (int p = 0; p < MAX_PLAYERS; ++p) {
                for (int32_t& v : pending.talks[p]) {
                    v = reader->Read_Int32();
                    if (v < 0) return fail("reputation save has a bad value");
                }
            }
        }
        if (reader->Failed() || pending.deeds_window < 0) return fail("reputation save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        state = pending;
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace reputation
