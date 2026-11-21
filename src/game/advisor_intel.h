#pragma once

#include <cstdint>
#include <string>

struct s_ship_design;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Intel memory (docs/LocalLLM/README.md, 5.5): what each empire saw of another empire's warships
// in battle — their weapons and their missile screen — so advised empires design ships against
// what they actually met rather than against technologies. Lives with the advisor: nothing is
// recorded while `advisor enabled` or `advisor rules_seen_ships` is off, and the memory is saved
// in its own optional save section, which older builds skip. Never draws from game_random.
namespace advisor {

    // Weapon classes, matching TECHDATA::weapons[].type (WEAPON_TYPE_BEAM..WEAPON_TYPE_SPECIAL).
    enum e_seen_weapon : uint8_t {
        SEEN_BEAM = 0,
        SEEN_MISSILE = 1,
        SEEN_TORPEDO = 2,
        SEEN_BOMB = 3,
        SEEN_FIGHTER = 4,
        SEEN_SPECIAL = 5,
        SEEN_WEAPON_COUNT = 6,
    };

    // What one empire remembers of another empire's warships. Weapon fields are damage potential
    // (mounts x maximum damage); missile_screen counts ECM jammers (1, 2 and 3 for the three
    // levels), lightning fields (3), anti-missile rocket mounts and point-defense mounts.
    struct s_seen_ships {
        uint16_t weapon[SEEN_WEAPON_COUNT] = {};
        uint16_t missile_screen = 0;
        uint16_t ships = 0;
    };

    // The threat a remembered fleet poses, by the share of its ship-to-ship damage (bombs left out).
    enum e_seen_threat : uint8_t {
        SEEN_THREAT_NONE = 0,
        SEEN_THREAT_MISSILES = 1,  // missiles and fighters at least half, missiles the larger part
        SEEN_THREAT_FIGHTERS = 2,  // missiles and fighters at least half, fighters the larger part
        SEEN_THREAT_BEAMS = 3,     // beams and torpedoes at least 60 %
    };

    // Counters halve their proportions before they would pass this, so shares survive saturation.
    constexpr uint16_t SEEN_LIMIT = 60000;
    // Every SEEN_DECAY_PERIOD turns each counter keeps SEEN_DECAY_KEEP / SEEN_DECAY_OUT_OF.
    constexpr int32_t SEEN_DECAY_PERIOD = 10;
    constexpr int32_t SEEN_DECAY_KEEP = 3;
    constexpr int32_t SEEN_DECAY_OUT_OF = 4;
    // Average missile_screen per seen ship from which missiles and fighters are not worth building.
    constexpr int32_t SEEN_SCREEN_PER_SHIP = 2;

    // Memory of `observer` about the warships of `owner` (a cleared record for invalid players).
    const s_seen_ships& Seen_Ships(uint8_t observer, uint8_t owner);
    bool Seen_Memory_Empty();
    void Clear_Seen_Ships();

    // `observer` saw one warship of `owner` built to `design`. Players >= MAX_PLAYERS and
    // observer == owner are ignored. Not gated by the flags: the battle hook checks them.
    void Observe_Design(uint8_t observer, uint8_t owner, const s_ship_design& design);

    // Battle hook (COMBFIND, before the battle is fought): when the advisor and rules_seen_ships
    // are on, each side remembers every ship of the other side. Ship indices are MOX::ship slots;
    // owners >= MAX_PLAYERS (monsters, Antarans) neither see nor are remembered.
    void Observe_Battle(const int16_t* attacker_ships, int16_t attacker_count,
        const int16_t* defender_ships, int16_t defender_count);

    // Once a turn from Run_Turn: every SEEN_DECAY_PERIOD stardates the memory fades.
    void Fade_Seen_Ships(int32_t stardate);

    // Pure decisions on one record.
    e_seen_threat Seen_Threat(const s_seen_ships& seen);
    bool Seen_Missile_Screen(const s_seen_ships& seen);

    // Design hooks (aidesign.cpp): the threat and missile screen that advised computer empire
    // `player` remembers of `enemy`; SEEN_THREAT_NONE / false when the advisor or rules_seen_ships
    // is off, for human players, players outside active_players and invalid enemies.
    e_seen_threat Design_Seen_Threat(uint8_t player, int16_t enemy);
    bool Design_Avoid_Missiles(uint8_t player, int16_t enemy);

    // Optional save section SAVEGAME::SECTION_TYPE_ADVISOR_INTEL: uint16 payload version, then
    // for every observer and owner the eight uint16 counters of s_seen_ships in field order.
    // Read_Seen_Save parses into `out` (MAX_PLAYERS x MAX_PLAYERS records) and leaves the memory
    // alone; Commit_Seen_Save installs the parsed records.
    inline constexpr uint16_t SEEN_SAVE_PAYLOAD_VERSION = 1;
    size_t Seen_Save_Size();
    void Write_Seen_Save(SERIAL::Writer_* writer);
    bool Read_Seen_Save(SERIAL::Reader_* reader, s_seen_ships* out, std::string* error);
    void Commit_Seen_Save(const s_seen_ships* records);

} // namespace advisor
