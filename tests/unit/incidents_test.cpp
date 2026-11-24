#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/bill.h"
#include "game/dice.h"
#include "game/incidents.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_incidents_scope {
        test::s_saved_value<bool> enabled{incidents::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_incidents_scope() {
            incidents::Clear();
            opinion::Clear();
            incidents::enabled = true;
            opinion::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_incidents_scope() {
            incidents::Clear();
            opinion::Clear();
        }
    };

    // Two computer empires, each with a colony (0: player 0 at star 0 with 4 people on a rich planet; 1: player 1
    // at star 1 with 3), player 0 holds a ship captain (leader 0 on ship 0) and a colony governor (leader 1).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_STARS = 3;
        MOX::NUM_COLONIES = 2;
        MOX::NUM_SHIPS = 1;
        MOX::stardate = 35001;
        for (int p = 0; p < 2; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].contact[1 - p] = 1;
        }
        for (int c = 0; c < 2; ++c) {
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->planets[c].mineral_class = 2;
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
            state->colonies[c].n_pops = static_cast<uint8_t>(4 - c);
        }
        state->ships[0].owner = 0;
        state->ships[0].status = 0;
        state->ships[0].location = 0;
        state->ships[0].officer_index = 0;
        state->leaders[0].type = LEADER_TYPE_SHIP;
        state->leaders[0].status = 1;
        state->leaders[0].player_index = 0;
        state->leaders[0].location = 0;
        state->leaders[1].type = LEADER_TYPE_COLONY;
        state->leaders[1].status = 1;
        state->leaders[1].player_index = 0;
        state->leaders[1].location = 0;
        state->stars[0].officer_index[0] = 1;
    }

    // A seed whose next Random(n) gives `value`.
    void Seed_For_(uint32_t n, uint32_t value) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            if (game_random::Random(n) == value) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    // A seed whose next `draws` rolls of 1..1000 all miss the chance of a new incident.
    void Calm_(int draws) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < draws; ++i) calm = calm && game_random::Random(1000) > static_cast<uint32_t>(incidents::EMPIRE_CHANCE);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    int32_t Morale_(const test::s_game_state& state, int c) {
        return incidents::Morale_Bonus(&state.colonies[c]);
    }
} // namespace

DOCTEST_TEST_SUITE("incidents") {

DOCTEST_TEST_CASE("flag off: nothing starts, the hooks are classic, no draw from game_random")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_PAINLESS, 0, incidents::PAINLESS_QUARANTINE));
    DOCTEST_CHECK(Morale_(state, 0) == incidents::PAINLESS_QUARANTINE_MORALE);
    incidents::enabled = false;
    DOCTEST_CHECK(Morale_(state, 0) == 0);
    DOCTEST_CHECK(incidents::Unrest(0) == 0);
    DOCTEST_CHECK_FALSE(incidents::Plague(0));
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_DEPOSIT, 0) == 0);
    for (int k = incidents::KIND_PAINLESS; k <= incidents::KIND_LAST; ++k) {
        DOCTEST_CHECK_FALSE(incidents::Start(static_cast<incidents::e_kind>(k), 0));
    }
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) incidents::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    incidents::Clear();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("a human takes the first option, a computer picks by personality")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_DEPOSIT, 0) == 0);
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_DEPOSIT, 0) == incidents::DEPOSIT_PLUNDER);
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_MAD_GOVERNOR, 0) == incidents::GOVERNOR_KEEP);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_TREASURY, 0) == incidents::TREASURY_RETURN);
    state.players[0].personality = PLAYER_PERSONALITY_ERRATIC;
    DOCTEST_CHECK(incidents::Choose(incidents::KIND_TREASURY, 0) == incidents::TREASURY_COMMISSION);
    // The choice is applied by Start: a ruthless computer plunders the deposit.
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_DEPOSIT, 0));
    DOCTEST_CHECK(state.planets[0].mineral_class == 4);
    DOCTEST_CHECK(incidents::Unrest(0) == incidents::DEPOSIT_PLUNDER_UNREST);
}

DOCTEST_TEST_CASE("disease without pain: cure, quarantine, levy")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_PAINLESS, 0, incidents::PAINLESS_CURE));
    DOCTEST_CHECK(state.players[0].bc == 1000 - incidents::PAINLESS_CURE_COST);
    DOCTEST_CHECK(incidents::Empty());
    // A poor empire cannot cure: it quarantines.
    state.players[0].bc = incidents::PAINLESS_CURE_COST;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_PAINLESS, 0, incidents::PAINLESS_CURE));
    DOCTEST_CHECK(state.players[0].bc == incidents::PAINLESS_CURE_COST);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::PAINLESS_QUARANTINE_MORALE);
    DOCTEST_CHECK_FALSE(incidents::Plague(0));
    DOCTEST_CHECK_FALSE(incidents::Start(incidents::KIND_PAINLESS, 0, incidents::PAINLESS_RECRUIT));  // the colony is busy
    MOX::stardate += incidents::PAINLESS_TURNS - 1;
    incidents::Run_Records();
    DOCTEST_CHECK(Morale_(state, 0) == incidents::PAINLESS_QUARANTINE_MORALE);
    MOX::stardate += 1;
    incidents::Run_Records();
    DOCTEST_CHECK(Morale_(state, 0) == 0);
    DOCTEST_CHECK(incidents::Empty());
    // The levy: the classic plague for a while, a lifted morale until the end.
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_PAINLESS, 0, incidents::PAINLESS_RECRUIT));
    DOCTEST_CHECK(incidents::Plague(0));
    DOCTEST_CHECK(Morale_(state, 0) == incidents::PAINLESS_RECRUIT_MORALE);
    MOX::stardate += incidents::PAINLESS_LEVY_TURNS;
    DOCTEST_CHECK_FALSE(incidents::Plague(0));
    DOCTEST_CHECK(Morale_(state, 0) == incidents::PAINLESS_RECRUIT_MORALE);
}

DOCTEST_TEST_CASE("drug of the natives: tolerate, tax, ban")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_NATIVE_DRUG, 0, incidents::DRUG_TOLERATE));
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DRUG_TOLERATE_MORALE);
    incidents::Run_Records();
    DOCTEST_CHECK(state.players[0].bc == 1000 - 4);
    MOX::stardate += incidents::DRUG_TURNS;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(Morale_(state, 0) == 0);

    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_NATIVE_DRUG, 0, incidents::DRUG_TAX));
    state.players[0].bc = 1000;
    incidents::Run_Records();
    DOCTEST_CHECK(state.players[0].bc == 1004);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DRUG_TAX_MORALE);
    incidents::Clear();
    incidents::enabled = true;

    // A ban: a worse morale, and smugglers on a hit of the dice.
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_NATIVE_DRUG, 0, incidents::DRUG_BAN));
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DRUG_BAN_MORALE);
    state.players[0].bc = 1000;
    Seed_For_(100, 100);  // a miss
    incidents::Run_Records();
    DOCTEST_CHECK(state.players[0].bc == 1000);
    Seed_For_(100, 1);  // a hit
    incidents::Run_Records();
    DOCTEST_CHECK(state.players[0].bc == 1000 - incidents::DRUG_CONTRABAND_LOSS);
}

DOCTEST_TEST_CASE("found treasury: returned, with a commission, or kept and found out")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_TREASURY, 0, incidents::TREASURY_RETURN));
    const int32_t amount = state.players[1].bc - 1000;
    DOCTEST_CHECK(amount >= incidents::TREASURY_MIN);
    DOCTEST_CHECK(amount <= incidents::TREASURY_MAX);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DID_US_A_SERVICE) > 0);
    DOCTEST_CHECK(incidents::Empty());

    state.players[1].bc = 1000;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_TREASURY, 0, incidents::TREASURY_COMMISSION));
    const int32_t returned = state.players[1].bc - 1000;
    const int32_t commission = state.players[0].bc - 1000;
    DOCTEST_CHECK(commission > 0);
    DOCTEST_CHECK(commission == (returned + commission) * incidents::TREASURY_COMMISSION_PERCENT / 100);

    state.players[0].bc = 1000;
    state.players[1].bc = 1000;
    opinion::Clear();
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_TREASURY, 0, incidents::TREASURY_KEEP));
    DOCTEST_CHECK(state.players[0].bc > 1000);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_REQUIRE(incidents::State()->count == 1);
    // No spies of the owner at the finder: the secret keeps (and no draw is made).
    const uint32_t seed = game_random::Get_Random_Seed();
    incidents::Run_Records();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(incidents::State()->count == 1);
    BILL::Set_Their_Spy_Number(1, 0, 3);
    Seed_For_(100, 100);
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::State()->count == 1);
    Seed_For_(100, 1);
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BAD_RUMOURS) < 0);
    // Without a leak the secret fades after TREASURY_SECRET_TURNS.
    BILL::Set_Their_Spy_Number(1, 0, 0);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_TREASURY, 0, incidents::TREASURY_KEEP));
    MOX::stardate += incidents::TREASURY_SECRET_TURNS;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
}

DOCTEST_TEST_CASE("rare deposit: leave, careful, plunder")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_DEPOSIT, 0, incidents::DEPOSIT_CAREFUL));
    DOCTEST_CHECK(state.planets[0].mineral_class == 3);
    DOCTEST_CHECK(incidents::Empty());

    state.planets[0].mineral_class = 2;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_DEPOSIT, 0, incidents::DEPOSIT_LEAVE));
    DOCTEST_CHECK(state.planets[0].mineral_class == 2);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DEPOSIT_LEAVE_MORALE);
    MOX::stardate += incidents::DEPOSIT_TURNS;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());

    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_DEPOSIT, 0, incidents::DEPOSIT_PLUNDER));
    DOCTEST_CHECK(state.planets[0].mineral_class == 4);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DEPOSIT_PLUNDER_MORALE);
    DOCTEST_CHECK(incidents::Unrest(0) == incidents::DEPOSIT_PLUNDER_UNREST);
    Seed_For_(100, 100);  // no earthquake
    incidents::Run_Records();
    DOCTEST_CHECK(state.colonies[0].n_pops == 4);
    MOX::stardate += incidents::DEPOSIT_PLUNDER_TURNS;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(incidents::Unrest(0) == 0);
    // An ultra rich planet holds no more deposit.
    state.planets[0].mineral_class = MINERAL_RESOURCE_ULTRA_RICH;
    DOCTEST_CHECK_FALSE(incidents::Start(incidents::KIND_DEPOSIT, 0, incidents::DEPOSIT_CAREFUL));
}

DOCTEST_TEST_CASE("rebel general: reasoning, execution, a coup or a trial after being ignored")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t good[3] = {10, 10, 0};
    const int32_t bad[3] = {1, 1, 0};
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_REASON, good));
    DOCTEST_CHECK(state.leaders[0].status == 1);
    DOCTEST_CHECK(state.ships[0].status == 0);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_REASON, bad));  // a coup
    DOCTEST_CHECK(state.leaders[0].status != 1);
    DOCTEST_CHECK(state.ships[0].status != 0);
    DOCTEST_CHECK(state.ships[0].officer_index == -1);
    DOCTEST_CHECK_FALSE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_EXECUTE));  // no captain left

    // Execution.
    state.ships[0].status = 0;
    state.ships[0].officer_index = 0;
    state.leaders[0].status = 1;
    state.leaders[0].location = 0;
    state.leaders[0].player_index = 0;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_EXECUTE));
    DOCTEST_CHECK(state.leaders[0].status != 1);
    DOCTEST_CHECK(state.ships[0].officer_index == -1);
    DOCTEST_CHECK(state.ships[0].status == 0);  // the ship stays

    // Ignored: a coup after GENERAL_RIPEN_TURNS ...
    state.ships[0].officer_index = 0;
    state.leaders[0].status = 1;
    state.leaders[0].location = 0;
    state.leaders[0].player_index = 0;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_IGNORE));
    DOCTEST_REQUIRE(incidents::State()->count == 1);
    Seed_For_(2, 1);
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::State()->count == 1);  // not yet ripe, no draw
    MOX::stardate += incidents::GENERAL_RIPEN_TURNS;
    Seed_For_(2, 1);
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(state.ships[0].status != 0);
    // ... or a trial.
    state.ships[0].status = 0;
    state.ships[0].officer_index = 0;
    state.leaders[0].status = 1;
    state.leaders[0].location = 0;
    state.leaders[0].player_index = 0;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_IGNORE));
    MOX::stardate += incidents::GENERAL_RIPEN_TURNS;
    Seed_For_(2, 2);
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(state.ships[0].status == 0);
    DOCTEST_CHECK(state.leaders[0].status != 1);
    // The general vanishing ends the record.
    state.ships[0].officer_index = 0;
    state.leaders[0].status = 1;
    state.leaders[0].location = 0;
    state.leaders[0].player_index = 0;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_REBEL_GENERAL, 0, incidents::GENERAL_IGNORE));
    state.leaders[0].status = 5;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
}

DOCTEST_TEST_CASE("mad governor: removed, or a boom and then a crash")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_MAD_GOVERNOR, 0, incidents::GOVERNOR_REMOVE));
    DOCTEST_CHECK(state.leaders[1].status != 1);
    DOCTEST_CHECK(incidents::Empty());

    state.leaders[1].status = 1;
    state.leaders[1].location = 0;
    state.leaders[1].player_index = 0;
    state.stars[0].officer_index[0] = 1;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_MAD_GOVERNOR, 0, incidents::GOVERNOR_KEEP));
    DOCTEST_CHECK(state.leaders[1].status == 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    incidents::Run_Records();
    DOCTEST_CHECK(state.players[0].bc == 1000 + 4 * incidents::GOVERNOR_BOOM_BC);
    DOCTEST_CHECK(Morale_(state, 0) == 0);
    MOX::stardate += incidents::GOVERNOR_BOOM_TURNS;
    incidents::Run_Records();  // the crash
    DOCTEST_CHECK(state.leaders[1].status != 1);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::GOVERNOR_CRASH_MORALE);
    DOCTEST_REQUIRE(incidents::State()->count == 1);
    DOCTEST_CHECK(incidents::State()->incident[0].stage == 1);
    MOX::stardate += incidents::GOVERNOR_CRASH_TURNS;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    DOCTEST_CHECK(Morale_(state, 0) == 0);

    // A governor taken away ends the boom early.
    state.leaders[1].status = 1;
    state.leaders[1].location = 0;
    state.leaders[1].player_index = 0;
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_MAD_GOVERNOR, 0, incidents::GOVERNOR_KEEP));
    state.leaders[1].status = 0;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
}

DOCTEST_TEST_CASE("a record whose colony changed hands ends; Turn starts nothing on a calm seed")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_NATIVE_DRUG, 0, incidents::DRUG_BAN));
    state.colonies[0].owner = 1;
    incidents::Run_Records();
    DOCTEST_CHECK(incidents::Empty());
    state.colonies[0].owner = 0;
    Calm_(2);
    incidents::Turn();
    DOCTEST_CHECK(incidents::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_incidents_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_DEPOSIT, 0, incidents::DEPOSIT_PLUNDER));
    DOCTEST_REQUIRE(incidents::Start(incidents::KIND_TREASURY, 0, incidents::TREASURY_KEEP));
    const uint32_t size = incidents::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    incidents::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    incidents::Clear();
    incidents::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(incidents::Parse_Save(&reader, &error));
    DOCTEST_CHECK(incidents::Empty());
    incidents::Commit_Save();
    DOCTEST_REQUIRE(incidents::State()->count == 2);
    DOCTEST_CHECK(incidents::State()->incident[0].kind == incidents::KIND_DEPOSIT);
    DOCTEST_CHECK(incidents::State()->incident[0].unrest == incidents::DEPOSIT_PLUNDER_UNREST);
    DOCTEST_CHECK(incidents::State()->incident[1].kind == incidents::KIND_TREASURY);
    DOCTEST_CHECK(incidents::State()->incident[1].other == 1);
    DOCTEST_CHECK(Morale_(state, 0) == incidents::DEPOSIT_PLUNDER_MORALE);

    // Record layout: version (2), count (1), then 20 bytes a record: kind 0, player 1, target 2-3, other 4-5,
    // started 6-9, ends 10-13, option 14, stage 15, morale 16, unrest 17, value 18-19.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !incidents::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));        // kind
    DOCTEST_CHECK(refuse(4, 9));        // player
    DOCTEST_CHECK(refuse(6, 0xFF));     // colony out of range (low byte; the high byte follows)
    DOCTEST_CHECK(refuse(17, 3));       // option
    DOCTEST_CHECK(refuse(18, 2));       // stage
    DOCTEST_CHECK(refuse(19, 40));      // morale
    DOCTEST_CHECK(refuse(20, 40));      // unrest
    DOCTEST_CHECK(refuse(2, 30));       // too many records
    DOCTEST_CHECK(refuse(0, 7));        // the version
    DOCTEST_CHECK(refuse(3 + 20 + 4, 9));  // the treasury's owner is no empire
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(incidents::Parse_Save(&r4, &error));
    DOCTEST_CHECK(incidents::State()->count == 2);  // the failed parses left the state alone
}

}
