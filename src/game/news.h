#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// News and ratings, news as intelligence (docs/LocalLLM/events-catalog.md 3.12; README.md 10.9, wave 3).
// The engine keeps a ring of the last NEWS_ITEMS facts of the galaxy (wars, treaties, conquests, new
// colonies, eliminations, council votes, new events, crises) and, every RATING_PERIOD turns, a ranking of
// the empires. An empire reads only the facts about empires it has contact with, and the galaxy-wide ones.
// With news_intelligence, spies add private facts for their reader: where a fleet gathers, whom an empire
// plans to attack. The advisor's view shows the last facts; the LLM writes the prose later.
// Wave 6, ab-review 2.13. The row (events-catalog 3.12): "GNN намекает, где копятся флоты или куда ведёт
// червоточина - при наличии шпионов". The author's decision: "при шпионах - новость о червоточине". Built:
// KIND_WORMHOLE, a private fact for a reader with spies at the empire that holds a wormhole shortcut
// (beasts.h): where the wormhole leads (value: the exit star).
// Both flags off by default; with them off nothing is recorded and nothing here draws from game_random.
namespace news {

    extern bool enabled;       // events news
    extern bool intelligence;  // events news_intelligence

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t NEWS_ITEMS = 32;
    constexpr int32_t RATING_PERIOD = 25;           // ratings are made on stardate % RATING_PERIOD == 0
    constexpr int32_t INTEL_PERIOD = 10;            // spies report on stardate % INTEL_PERIOD == INTEL_PHASE
    constexpr int32_t INTEL_PHASE = 5;
    constexpr int32_t GATHER_DIVISOR = 3;           // a star with at least a third of the fleet is a gathering
    constexpr int32_t EYES_ON_PER_MILLE_PER_SPY = 20;
    constexpr int32_t MAX_SPIES = 5;
    constexpr int32_t VIEW_ITEMS = 8;               // facts in the advisor's view

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_WAR = 1,             // subject declared war on object
        KIND_PEACE = 2,           // subject and object made peace
        KIND_ALLIANCE = 3,        // subject and object made an alliance
        KIND_COLONY_TAKEN = 4,    // subject took a colony (value) of object by invasion
        KIND_COLONY_FOUNDED = 5,  // subject founded a colony (value)
        KIND_ELIMINATED = 6,      // subject is eliminated
        KIND_COUNCIL = 7,         // the council vote: subject elected (-1: nobody)
        KIND_EVENT = 8,           // a new event (world_events): subject the empire or -1; value = e_kind of the event
        KIND_CRISIS_START = 9,    // subject's crisis (value = crises::e_crisis) opened
        KIND_CRISIS_END = 10,     // subject's crisis (value / 10) ended with the result (value % 10)
        KIND_RATINGS = 11,        // leaders: subject by population, object by fleet, value by technologies
        KIND_FLEETS_GATHER = 12,  // private (intelligence): subject's fleets gather at star (value)
        KIND_EYES_ON = 13,        // private (intelligence): subject has its eyes on object
        KIND_WORMHOLE = 14,       // private (intelligence): subject's wormhole leads to star (value)
        KIND_COUNT = 15,
    };

    struct s_item {
        int32_t stardate = 0;
        uint8_t kind = KIND_NONE;
        int8_t subject = -1;
        int8_t object = -1;
        int8_t reader = -1;     // private to this empire; -1 for public news
        int16_t value = 0;
    };

    // The empires ranked by population, fleet strength (warship strength) and known technologies, best first;
    // -1 pads the order when fewer empires live. `value` is the figure each stands on.
    constexpr int32_t RANK_SLOTS = 8;  // the engine's MAX_PLAYERS (checked in news_state.cpp)
    enum e_rating : uint8_t { RATING_POPULATION = 0, RATING_FLEET = 1, RATING_TECHNOLOGY = 2, RATING_COUNT = 3 };
    struct s_ratings {
        int32_t stardate = 0;  // 0: no ranking yet
        int8_t order[RATING_COUNT][RANK_SLOTS] = {};
        int32_t value[RATING_COUNT][RANK_SLOTS] = {};
    };

    // The ring (news_state.cpp; no game logic): appends a fact, dropping the oldest when NEWS_ITEMS are held;
    // whether an identical fact is held; the stored ranking.
    void Push_Item(const s_item& item);
    bool Contains(const s_item& item);
    void Set_Ratings(const s_ratings& ratings);
    // Eliminations already noticed by Turn (not saved: Turn takes the first look after a load as the baseline).
    struct s_watch {
        bool seeded = false;
        uint8_t eliminated = 0;  // bit k: empire k was eliminated when last looked at
    };
    s_watch* Watch();

    // Records a fact; does nothing with the flag off. `reader` >= 0 makes it private to that empire.
    void Add(e_kind kind, int16_t subject, int16_t object, int32_t value, int16_t reader = -1);

    // Whether `viewer` may read `item`: private ones only by their reader; public ones when the viewer is
    // one of the empires named or has contact with one of them, and the galaxy-wide ones (no empire named,
    // ratings, council votes) always.
    bool Visible_To(const s_item& item, int16_t viewer);
    // The newest facts `viewer` may read, newest first; returns how many were written (at most `max`).
    int32_t Recent_For(int16_t viewer, s_item* out, int32_t max);
    // Facts held (at most NEWS_ITEMS), and the one `age` places back from the newest (0 = newest); nullptr
    // when there is none.
    int32_t Count();
    const s_item* Nth_Newest(int32_t age);
    const s_ratings& Ratings();

    // One line of the fact, for the advisor's view: "stardate 3512: player 2 declared war on player 5".
    std::string Describe(const s_item& item);

    // The last ranking as `viewer` may read it (itself and the empires it has contact with, in rank order):
    // "stardate 35100: population 1 > 0; fleet 0 > 1; technologies 1 > 0"; empty when there is none.
    std::string Describe_Ratings(int16_t viewer);

    // Makes the rating now (Turn does it every RATING_PERIOD turns) and records the "ratings" fact.
    void Make_Ratings();
    // The star where `subject`'s warships are strongest, if that is at least a third of its fleet; else -1.
    int32_t Gathering_Star(int16_t subject);
    // "Eyes on": the empire `subject`'s original AI plans to attack, -1 when none.
    int16_t Planned_Victim(int16_t subject);
    // The private facts of `reader` about `subject` (needs spies there): the gathering, and with `roll`
    // (0-999) under EYES_ON_PER_MILLE_PER_SPY a spy, the plan. True when a fact was recorded.
    bool Report_Gathering(int16_t reader, int16_t subject);
    bool Report_Eyes_On(int16_t reader, int16_t subject, int32_t roll);
    // The private fact of `reader` about the wormhole `subject` holds (needs spies there and events beasts on).
    bool Report_Wormhole(int16_t reader, int16_t subject);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): eliminations noticed, ratings made,
    // and with intelligence the spies' facts (the only draws from game_random: one per spied plan).
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'NEWS' (game/rules_sections.h): payload version 2 (version 1, without the wormhole fact, is read too), then the ratings (stardate int32, then
    // per rating and slot order int8 and value int32), the count (uint8) and per fact, oldest first,
    // stardate (int32), kind, subject, object, reader (int8 / uint8), value (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace news
