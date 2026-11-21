#include "pch.h"

#include "game/advisor_intel.h"

#include "game/advisor.h"
#include "game/serialization.h"
#include "game/techdata.h"

// Nothing in this file draws from game_random.
namespace advisor {
    namespace {
        constexpr size_t SEEN_FIELDS = SEEN_WEAPON_COUNT + 2;
        constexpr size_t SEEN_SAVE_SIZE =
            sizeof(uint16_t) + MAX_PLAYERS * MAX_PLAYERS * SEEN_FIELDS * sizeof(uint16_t);

        s_seen_ships seen[MAX_PLAYERS][MAX_PLAYERS];
        const s_seen_ships no_record;

        bool Has_Special_(const s_ship_design& design, int32_t special) {
            if (special < 0 || special >= static_cast<int32_t>(sizeof(design.special_device_flags) * 8)) return false;
            return ((static_cast<uint8_t>(design.special_device_flags[special >> 3]) >> (special & 7)) & 1) != 0;
        }

        // Pointers to the counters of a record in save order.
        void Fields_(s_seen_ships* record, uint16_t* fields[SEEN_FIELDS]) {
            for (int i = 0; i < SEEN_WEAPON_COUNT; ++i) fields[i] = &record->weapon[i];
            fields[SEEN_WEAPON_COUNT] = &record->missile_screen;
            fields[SEEN_WEAPON_COUNT + 1] = &record->ships;
        }

        // Adds `add` (same layout as the record) and halves the record first while a sum would
        // pass SEEN_LIMIT, so the proportions survive.
        void Add_(s_seen_ships* record, const uint32_t add[SEEN_FIELDS]) {
            uint16_t* fields[SEEN_FIELDS];
            Fields_(record, fields);
            for (;;) {
                bool fits = true;
                for (size_t i = 0; i < SEEN_FIELDS; ++i) {
                    if (static_cast<uint32_t>(*fields[i]) + add[i] > SEEN_LIMIT) fits = false;
                }
                if (fits) break;
                bool any = false;
                for (size_t i = 0; i < SEEN_FIELDS; ++i) {
                    if (*fields[i] != 0) any = true;
                    *fields[i] = static_cast<uint16_t>(*fields[i] / 2);
                }
                if (!any) return;  // one ship alone exceeds the limit: ignore it
            }
            for (size_t i = 0; i < SEEN_FIELDS; ++i) *fields[i] = static_cast<uint16_t>(*fields[i] + add[i]);
        }

        bool Steered_Computer_(uint8_t player) {
            if (!enabled || !rules_seen_ships || player >= MAX_PLAYERS) return false;
            if (player >= game::current.Player_Count()) return false;
            if (((active_players >> player) & 1) == 0) return false;
            return game::current.players[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }
    } // namespace

    const s_seen_ships& Seen_Ships(uint8_t observer, uint8_t owner) {
        if (observer >= MAX_PLAYERS || owner >= MAX_PLAYERS) return no_record;
        return seen[observer][owner];
    }

    bool Seen_Memory_Empty() {
        for (const auto& row : seen) {
            for (const s_seen_ships& record : row) {
                if (record.ships != 0 || record.missile_screen != 0) return false;
                for (uint16_t value : record.weapon) {
                    if (value != 0) return false;
                }
            }
        }
        return true;
    }

    void Clear_Seen_Ships() {
        for (auto& row : seen) {
            for (s_seen_ships& record : row) record = s_seen_ships{};
        }
    }

    void Observe_Design(uint8_t observer, uint8_t owner, const s_ship_design& design) {
        if (observer >= MAX_PLAYERS || owner >= MAX_PLAYERS || observer == owner) return;
        uint32_t add[SEEN_FIELDS] = {};
        for (const s_ship_weapons& mount : design.ship_weapon) {
            if (mount.type <= WEAPON_NO_WEAPONS || mount.type >= WEAPON_COUNT || mount.count <= 0) continue;
            const uint32_t count = static_cast<uint32_t>(mount.count);
            const bool point_defense = (mount.specials & WEAPON_MOD_FLAG_POINT_DEFENSE) != 0;
            if (mount.type == WEAPON_ANTI_MISSILE_ROCKET || point_defense) {
                add[SEEN_WEAPON_COUNT] += count;  // a screen, not a threat to ships
                continue;
            }
            const uint8_t type = TECHDATA::weapons[mount.type].type;
            if (type >= SEEN_WEAPON_COUNT) continue;
            add[type] += count * TECHDATA::weapons[mount.type].max_damage;
        }
        if (Has_Special_(design, SPECIAL_ECM_JAMMER)) add[SEEN_WEAPON_COUNT] += 1;
        if (Has_Special_(design, SPECIAL_MULTI_WAVE_ECM_JAMMER)) add[SEEN_WEAPON_COUNT] += 2;
        if (Has_Special_(design, SPECIAL_WIDE_AREA_JAMMER)) add[SEEN_WEAPON_COUNT] += 3;
        if (Has_Special_(design, SPECIAL_LIGHTNING_FIELD)) add[SEEN_WEAPON_COUNT] += 3;
        add[SEEN_WEAPON_COUNT + 1] = 1;
        Add_(&seen[observer][owner], add);
    }

    void Observe_Battle(const int16_t* attacker_ships, int16_t attacker_count,
        const int16_t* defender_ships, int16_t defender_count) {
        if (!enabled || !rules_seen_ships || MOX::ship == nullptr) return;
        const auto owner_of = [](int16_t ship) -> int32_t {
            if (ship < 0 || ship >= MOX::NUM_SHIPS) return -1;
            return MOX::ship[ship].owner;
        };
        // Each side sees every warship of the other side once per battle.
        const auto watch = [&](const int16_t* watchers, int16_t watcher_count, const int16_t* ships, int16_t ship_count) {
            if (watchers == nullptr || ships == nullptr) return;
            uint32_t observers = 0;
            for (int16_t i = 0; i < watcher_count; ++i) {
                const int32_t owner = owner_of(watchers[i]);
                if (owner >= 0 && owner < MAX_PLAYERS) observers |= 1u << owner;
            }
            for (int16_t i = 0; i < ship_count; ++i) {
                const int32_t owner = owner_of(ships[i]);
                if (owner < 0 || owner >= MAX_PLAYERS) continue;
                const s_ship_data& ship = MOX::ship[ships[i]];
                if (ship.d.ship_type != SHIP_TYPE_COMBAT) continue;
                for (uint8_t observer = 0; observer < MAX_PLAYERS; ++observer) {
                    if ((observers >> observer) & 1) Observe_Design(observer, static_cast<uint8_t>(owner), ship.d);
                }
            }
        };
        watch(attacker_ships, attacker_count, defender_ships, defender_count);
        watch(defender_ships, defender_count, attacker_ships, attacker_count);
    }

    void Fade_Seen_Ships(int32_t stardate) {
        if (stardate % SEEN_DECAY_PERIOD != 0) return;
        for (auto& row : seen) {
            for (s_seen_ships& record : row) {
                uint16_t* fields[SEEN_FIELDS];
                Fields_(&record, fields);
                for (uint16_t* field : fields) {
                    *field = static_cast<uint16_t>(static_cast<uint32_t>(*field) * SEEN_DECAY_KEEP / SEEN_DECAY_OUT_OF);
                }
            }
        }
    }

    e_seen_threat Seen_Threat(const s_seen_ships& record) {
        const int32_t beams = record.weapon[SEEN_BEAM] + record.weapon[SEEN_TORPEDO];
        const int32_t missiles = record.weapon[SEEN_MISSILE];
        const int32_t fighters = record.weapon[SEEN_FIGHTER];
        const int32_t total = beams + missiles + fighters + record.weapon[SEEN_SPECIAL];
        if (record.ships == 0 || total == 0) return SEEN_THREAT_NONE;
        if ((missiles + fighters) * 2 >= total) return fighters > missiles ? SEEN_THREAT_FIGHTERS : SEEN_THREAT_MISSILES;
        if (beams * 10 >= total * 6) return SEEN_THREAT_BEAMS;
        return SEEN_THREAT_NONE;
    }

    bool Seen_Missile_Screen(const s_seen_ships& record) {
        return record.ships != 0 &&
            static_cast<int32_t>(record.missile_screen) >= SEEN_SCREEN_PER_SHIP * static_cast<int32_t>(record.ships);
    }

    e_seen_threat Design_Seen_Threat(uint8_t player, int16_t enemy) {
        if (!Steered_Computer_(player) || enemy < 0 || enemy >= MAX_PLAYERS || enemy == player) return SEEN_THREAT_NONE;
        return Seen_Threat(seen[player][enemy]);
    }

    bool Design_Avoid_Missiles(uint8_t player, int16_t enemy) {
        if (!Steered_Computer_(player) || enemy < 0 || enemy >= MAX_PLAYERS || enemy == player) return false;
        return Seen_Missile_Screen(seen[player][enemy]);
    }

    size_t Seen_Save_Size() {
        return SEEN_SAVE_SIZE;
    }

    void Write_Seen_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SEEN_SAVE_PAYLOAD_VERSION);
        for (auto& row : seen) {
            for (s_seen_ships& record : row) {
                uint16_t* fields[SEEN_FIELDS];
                Fields_(&record, fields);
                for (uint16_t* field : fields) writer->Write_Uint16(*field);
            }
        }
    }

    bool Read_Seen_Save(SERIAL::Reader_* reader, s_seen_ships* out, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            return false;
        };
        if (reader == nullptr || out == nullptr || reader->Remaining() < sizeof(uint16_t)) {
            return fail("intel memory save is truncated");
        }
        if (reader->Read_Uint16() != SEEN_SAVE_PAYLOAD_VERSION) return fail("unsupported intel memory payload version");
        if (reader->Remaining() != SEEN_SAVE_SIZE - sizeof(uint16_t)) return fail("intel memory save has the wrong size");
        for (size_t i = 0; i < MAX_PLAYERS * MAX_PLAYERS; ++i) {
            out[i] = s_seen_ships{};
            uint16_t* fields[SEEN_FIELDS];
            Fields_(&out[i], fields);
            for (uint16_t* field : fields) {
                *field = reader->Read_Uint16();
                if (*field > SEEN_LIMIT) return fail("intel memory save has an out-of-range value");
            }
        }
        if (reader->Failed()) return fail("intel memory save is truncated");
        return true;
    }

    void Commit_Seen_Save(const s_seen_ships* records) {
        if (records == nullptr) {
            Clear_Seen_Ships();
            return;
        }
        for (size_t observer = 0; observer < MAX_PLAYERS; ++observer) {
            for (size_t owner = 0; owner < MAX_PLAYERS; ++owner) seen[observer][owner] = records[observer * MAX_PLAYERS + owner];
        }
    }

} // namespace advisor
