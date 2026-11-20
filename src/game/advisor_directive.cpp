#include "pch.h"

#include "game/advisor_directive.h"

#include "game/serialization.h"

namespace advisor {
    namespace {
        // s_player::treaty[] values: 0 none, 1 non-aggression pact, 2 alliance (Start_Treaty from
        // the diplomacy screen, dip_scrn_main.cpp:1366-1369), 3 peace (Declare_Peace), 4-6 war
        // (Declare_War, diplomac.cpp:1121-1129). The "TREATY_ALLIANCE" comment next to `>= 4` in
        // council.cpp:720 is wrong: that test means war.
        constexpr int ALLIANCE_TREATY = 2;
        constexpr uint8_t LAST_OBJECTIVE = PLAYER_OBJECTIVE_ECOLOGIST;

        // source 1, issued 4, expires 4, objective 1, posture 1, attitude MAX_PLAYERS, main_rival 1,
        // weights WEIGHT_COUNT, research 1, doctrine 1, spy_target 1, spy_mission 1, rule_id 2.
        constexpr size_t DIRECTIVE_SAVE_SIZE =
            1 + 4 + 4 + 1 + 1 + MAX_PLAYERS + 1 + WEIGHT_COUNT + 1 + 1 + 1 + 1 + 2;
        constexpr size_t DIRECTIVES_SAVE_SIZE = MAX_PLAYERS * DIRECTIVE_SAVE_SIZE;
        // Payload v1: version + directives. v2 appends int32 last_objective_change per player.
        constexpr size_t SAVE_SIZE_V1 = sizeof(uint16_t) + DIRECTIVES_SAVE_SIZE;
        constexpr size_t SAVE_SIZE = SAVE_SIZE_V1 + MAX_PLAYERS * sizeof(int32_t);

        s_directive directives[MAX_PLAYERS];
        int32_t last_objective_change[MAX_PLAYERS] = {};

        bool Fail_(std::string* reason, const char* text) {
            if (reason != nullptr) *reason = text;
            return false;
        }

        bool Is_Ally_(const game::s_view* world, uint8_t player, uint8_t other) {
            return world->players[player].treaty[other] == ALLIANCE_TREATY;
        }

        // other must be another living player this player has met.
        bool Is_Valid_Target_(const game::s_view* world, uint8_t player, uint8_t other) {
            if (other >= MAX_PLAYERS || other == player || other >= world->Player_Count()) return false;
            if (world->players[other].eliminated != 0) return false;
            return world->players[player].contact[other] != 0;
        }

        void Write_Directive_(SERIAL::Writer_* writer, const s_directive* directive) {
            writer->Write_Uint8(directive->source);
            writer->Write_Int32(directive->issued_stardate);
            writer->Write_Int32(directive->expires_stardate);
            writer->Write_Uint8(directive->objective);
            writer->Write_Uint8(directive->posture);
            for (int i = 0; i < MAX_PLAYERS; ++i) writer->Write_Uint8(directive->attitude[i]);
            writer->Write_Uint8(directive->main_rival);
            for (int i = 0; i < WEIGHT_COUNT; ++i) writer->Write_Uint8(directive->weights[i]);
            writer->Write_Uint8(directive->research_focus);
            writer->Write_Uint8(directive->fleet_doctrine);
            writer->Write_Uint8(directive->spy_target);
            writer->Write_Uint8(directive->spy_mission);
            writer->Write_Uint16(directive->rule_id);
        }

        void Read_Directive_(SERIAL::Reader_* reader, s_directive* directive) {
            directive->source = reader->Read_Uint8();
            directive->issued_stardate = reader->Read_Int32();
            directive->expires_stardate = reader->Read_Int32();
            directive->objective = reader->Read_Uint8();
            directive->posture = reader->Read_Uint8();
            for (int i = 0; i < MAX_PLAYERS; ++i) directive->attitude[i] = reader->Read_Uint8();
            directive->main_rival = reader->Read_Uint8();
            for (int i = 0; i < WEIGHT_COUNT; ++i) directive->weights[i] = reader->Read_Uint8();
            directive->research_focus = reader->Read_Uint8();
            directive->fleet_doctrine = reader->Read_Uint8();
            directive->spy_target = reader->Read_Uint8();
            directive->spy_mission = reader->Read_Uint8();
            directive->rule_id = reader->Read_Uint16();
        }

        // Enum ranges and player-index ranges only; no world state.
        bool Fields_In_Range_(const s_directive* directive) {
            if (directive->source > SOURCE_DEBUG) return false;
            if (directive->objective != OBJECTIVE_KEEP && directive->objective > LAST_OBJECTIVE) return false;
            if (directive->posture >= POSTURE_COUNT) return false;
            for (int i = 0; i < MAX_PLAYERS; ++i) {
                if (directive->attitude[i] >= ATTITUDE_COUNT) return false;
            }
            if (directive->main_rival != NO_PLAYER && directive->main_rival >= MAX_PLAYERS) return false;
            for (int i = 0; i < WEIGHT_COUNT; ++i) {
                if (directive->weights[i] >= LEVEL_COUNT) return false;
            }
            if (directive->research_focus >= RESEARCH_COUNT) return false;
            if (directive->fleet_doctrine >= DOCTRINE_COUNT) return false;
            if (directive->spy_target != NO_PLAYER && directive->spy_target >= MAX_PLAYERS) return false;
            return directive->spy_mission < SPY_COUNT;
        }
    } // namespace

    void Clear_Directive(s_directive* directive) {
        *directive = s_directive{};
    }

    bool Is_Active(const s_directive* directive, int32_t stardate) {
        return directive->source != SOURCE_NONE &&
            directive->issued_stardate <= stardate &&
            stardate <= directive->expires_stardate;
    }

    bool Validate_Directive(const game::s_view* world,
        uint8_t player,
        const s_directive* directive,
        std::string* reason) {
        if (world == nullptr || world->players == nullptr || directive == nullptr ||
            player >= MAX_PLAYERS || player >= world->Player_Count()) {
            return Fail_(reason, "invalid world, player or directive");
        }
        if (directive->objective == PLAYER_OBJECTIVE_HUMAN) {
            return Fail_(reason, "the human objective cannot be set");
        }
        if (!Fields_In_Range_(directive)) {
            return Fail_(reason, "a field is out of range");
        }
        if (directive->main_rival != NO_PLAYER && !Is_Valid_Target_(world, player, directive->main_rival)) {
            return Fail_(reason, "main_rival must be another living player with contact");
        }
        if (directive->spy_target != NO_PLAYER && !Is_Valid_Target_(world, player, directive->spy_target)) {
            return Fail_(reason, "spy_target must be another living player with contact");
        }
        for (uint8_t other = 0; other < MAX_PLAYERS; ++other) {
            const uint8_t attitude = directive->attitude[other];
            if (attitude == ATTITUDE_KEEP) continue;
            if (other == player || other >= world->Player_Count() ||
                world->players[player].contact[other] == 0) {
                return Fail_(reason, "attitude set toward a player without contact");
            }
            if ((attitude == ATTITUDE_WAR || attitude == ATTITUDE_HOSTILE) &&
                Is_Ally_(world, player, other)) {
                return Fail_(reason, "war or hostile attitude toward an ally");
            }
        }
        if (directive->posture == POSTURE_START_NEW_WAR) {
            if (directive->main_rival == NO_PLAYER) {
                return Fail_(reason, "START_NEW_WAR needs a main_rival");
            }
            if (Is_Ally_(world, player, directive->main_rival)) {
                return Fail_(reason, "START_NEW_WAR against an ally");
            }
        }
        return true;
    }

    const s_directive* Get_Directive(uint8_t player) {
        return player < MAX_PLAYERS ? &directives[player] : nullptr;
    }

    void Set_Directive(uint8_t player, const s_directive* directive) {
        if (player >= MAX_PLAYERS || directive == nullptr) return;
        directives[player] = *directive;
    }

    void Clear_All_Directives() {
        for (s_directive& directive : directives) Clear_Directive(&directive);
        for (int32_t& stardate : last_objective_change) stardate = 0;
    }

    int32_t Last_Objective_Change(uint8_t player) {
        return player < MAX_PLAYERS ? last_objective_change[player] : 0;
    }

    void Set_Last_Objective_Change(uint8_t player, int32_t stardate) {
        if (player < MAX_PLAYERS) last_objective_change[player] = stardate;
    }

    size_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_directive& directive : directives) Write_Directive_(writer, &directive);
        for (int32_t stardate : last_objective_change) writer->Write_Int32(stardate);
    }

    bool Read_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) {
            return fail("advisor save is truncated");
        }
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) {
            return fail("unsupported advisor payload version");
        }
        const size_t expected = version == 1 ? DIRECTIVES_SAVE_SIZE : SAVE_SIZE - sizeof(uint16_t);
        if (reader->Remaining() < expected) {
            return fail("advisor save is truncated");
        }
        if (reader->Remaining() > expected) {
            return fail("advisor save has trailing bytes");
        }
        s_directive parsed[MAX_PLAYERS];
        for (s_directive& directive : parsed) {
            Read_Directive_(reader, &directive);
            if (!Fields_In_Range_(&directive)) return fail("advisor save has an out-of-range value");
        }
        int32_t parsed_changes[MAX_PLAYERS] = {};
        if (version >= 2) {
            for (int32_t& stardate : parsed_changes) stardate = reader->Read_Int32();
        }
        if (reader->Failed()) return fail("advisor save is truncated");
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            directives[i] = parsed[i];
            last_objective_change[i] = parsed_changes[i];
        }
        return true;
    }

} // namespace advisor
