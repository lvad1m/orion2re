#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor_directive.h"
#include "game/serialization.h"

namespace {
    struct s_store_scope {
        s_store_scope() {
            advisor::Clear_All_Directives();
        }
        ~s_store_scope() {
            advisor::Clear_All_Directives();
        }
    };

    // Players 0..3 exist. Player 0 has met 1, 2 and 3; 1 is an ally; 2 is eliminated.
    void Setup_World_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
        }
        state->players[0].contact[1] = 1;
        state->players[0].contact[2] = 1;
        state->players[0].contact[3] = 1;
        state->players[0].treaty[1] = 2;  // alliance (4-6 would be war)
        state->players[2].eliminated = 1;
    }

    bool Valid_(const advisor::s_directive& directive, std::string* reason = nullptr) {
        return advisor::Validate_Directive(&game::current, 0, &directive, reason);
    }

    advisor::s_directive Sample_Directive_() {
        advisor::s_directive directive;
        directive.source = advisor::SOURCE_DEBUG;
        directive.issued_stardate = 100;
        directive.expires_stardate = 110;
        directive.objective = PLAYER_OBJECTIVE_ECOLOGIST;
        directive.posture = advisor::POSTURE_ATTACK_CURRENT_ENEMIES;
        directive.attitude[3] = advisor::ATTITUDE_HOSTILE;
        directive.main_rival = 3;
        directive.weights[advisor::WEIGHT_FLEET] = advisor::LEVEL_MAX;
        directive.weights[advisor::WEIGHT_RESEARCH] = advisor::LEVEL_LOW;
        directive.research_focus = advisor::RESEARCH_PROPULSION;
        directive.fleet_doctrine = advisor::DOCTRINE_ANTI_BEAM;
        directive.spy_target = 3;
        directive.spy_mission = advisor::SPY_STEAL_TECH;
        directive.rule_id = 0xABCD;
        return directive;
    }

    std::vector<uint8_t> Saved_Bytes_() {
        std::vector<uint8_t> bytes(advisor::Save_Size());
        SERIAL::Writer_ writer(bytes.data(), static_cast<uint32_t>(bytes.size()), "test advisor overflow");
        advisor::Write_Save(&writer);
        DOCTEST_REQUIRE(writer.Length() == bytes.size());
        return bytes;
    }

    bool Load_(const std::vector<uint8_t>& bytes, std::string* error) {
        SERIAL::Reader_ reader(bytes.data(), static_cast<uint32_t>(bytes.size()), "test advisor underflow", true);
        return advisor::Read_Save(&reader, error);
    }
} // namespace

DOCTEST_TEST_CASE("directive clear and activity window") {
    advisor::s_directive directive = Sample_Directive_();
    DOCTEST_CHECK_FALSE(advisor::Is_Active(&directive, 99));
    DOCTEST_CHECK(advisor::Is_Active(&directive, 100));
    DOCTEST_CHECK(advisor::Is_Active(&directive, 110));
    DOCTEST_CHECK_FALSE(advisor::Is_Active(&directive, 111));

    directive.source = advisor::SOURCE_NONE;
    DOCTEST_CHECK_FALSE(advisor::Is_Active(&directive, 105));

    directive = Sample_Directive_();
    advisor::Clear_Directive(&directive);
    DOCTEST_CHECK(directive.source == advisor::SOURCE_NONE);
    DOCTEST_CHECK(directive.issued_stardate == 0);
    DOCTEST_CHECK(directive.objective == advisor::OBJECTIVE_KEEP);
    DOCTEST_CHECK(directive.main_rival == advisor::NO_PLAYER);
    DOCTEST_CHECK(directive.spy_target == advisor::NO_PLAYER);
    DOCTEST_CHECK(directive.attitude[3] == advisor::ATTITUDE_KEEP);
    DOCTEST_CHECK(directive.rule_id == 0);
    DOCTEST_CHECK_FALSE(advisor::Is_Active(&directive, 0));
}

DOCTEST_TEST_CASE("Validate_Directive accepts a well formed directive and an empty one") {
    test::s_game_state state;
    Setup_World_(&state);
    std::string reason;
    DOCTEST_CHECK(Valid_(Sample_Directive_(), &reason));
    advisor::s_directive empty;
    DOCTEST_CHECK(Valid_(empty));
}

DOCTEST_TEST_CASE("Validate_Directive rejects out of range fields") {
    test::s_game_state state;
    Setup_World_(&state);
    std::string reason;

    advisor::s_directive directive;
    directive.source = 4;
    DOCTEST_CHECK_FALSE(Valid_(directive, &reason));
    DOCTEST_CHECK_FALSE(reason.empty());

    directive = advisor::s_directive{};
    directive.posture = advisor::POSTURE_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.attitude[3] = advisor::ATTITUDE_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.weights[advisor::WEIGHT_FOOD] = advisor::LEVEL_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.research_focus = advisor::RESEARCH_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.fleet_doctrine = advisor::DOCTRINE_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.spy_mission = advisor::SPY_COUNT;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.main_rival = MAX_PLAYERS;
    DOCTEST_CHECK_FALSE(Valid_(directive));

    directive = advisor::s_directive{};
    directive.spy_target = MAX_PLAYERS;
    DOCTEST_CHECK_FALSE(Valid_(directive));
}

DOCTEST_TEST_CASE("Validate_Directive checks the objective") {
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive;
    for (uint8_t objective = 0; objective <= 5; ++objective) {
        directive.objective = objective;
        DOCTEST_CHECK(Valid_(directive));
    }
    directive.objective = 6;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.objective = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.objective = advisor::OBJECTIVE_KEEP;
    DOCTEST_CHECK(Valid_(directive));
}

DOCTEST_TEST_CASE("Validate_Directive checks main_rival and spy_target") {
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive;

    directive.main_rival = 0;  // self
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.main_rival = 2;  // eliminated
    DOCTEST_CHECK_FALSE(Valid_(directive));
    state.players[0].contact[3] = 0;
    directive.main_rival = 3;  // no contact
    DOCTEST_CHECK_FALSE(Valid_(directive));
    state.players[0].contact[3] = 1;
    directive.main_rival = 4;  // not in the game
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.main_rival = 3;
    DOCTEST_CHECK(Valid_(directive));

    directive = advisor::s_directive{};
    directive.spy_target = 0;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.spy_target = 2;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    state.players[0].contact[3] = 0;
    directive.spy_target = 3;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    state.players[0].contact[3] = 1;
    DOCTEST_CHECK(Valid_(directive));
}

DOCTEST_TEST_CASE("Validate_Directive checks attitudes against contact and alliances") {
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive;

    directive.attitude[1] = advisor::ATTITUDE_WAR;  // ally
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.attitude[1] = advisor::ATTITUDE_HOSTILE;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.attitude[1] = advisor::ATTITUDE_WARY;
    DOCTEST_CHECK(Valid_(directive));
    directive.attitude[1] = advisor::ATTITUDE_ALLY;
    DOCTEST_CHECK(Valid_(directive));

    directive = advisor::s_directive{};
    state.players[0].contact[3] = 0;
    directive.attitude[3] = advisor::ATTITUDE_FRIENDLY;
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.attitude[3] = advisor::ATTITUDE_KEEP;
    DOCTEST_CHECK(Valid_(directive));

    state.players[0].contact[3] = 1;
    directive.attitude[3] = advisor::ATTITUDE_WAR;  // met, no alliance
    DOCTEST_CHECK(Valid_(directive));
}

DOCTEST_TEST_CASE("Validate_Directive checks START_NEW_WAR") {
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive;
    directive.posture = advisor::POSTURE_START_NEW_WAR;
    std::string reason;
    DOCTEST_CHECK_FALSE(Valid_(directive, &reason));  // no main_rival
    DOCTEST_CHECK_FALSE(reason.empty());
    directive.main_rival = 1;  // ally
    DOCTEST_CHECK_FALSE(Valid_(directive));
    directive.main_rival = 3;
    DOCTEST_CHECK(Valid_(directive));
}

DOCTEST_TEST_CASE("directive store round trips through the save payload") {
    s_store_scope scope;
    DOCTEST_CHECK(advisor::Get_Directive(MAX_PLAYERS) == nullptr);
    DOCTEST_CHECK(advisor::Get_Directive(0)->source == advisor::SOURCE_NONE);

    const advisor::s_directive first = Sample_Directive_();
    advisor::s_directive second = Sample_Directive_();
    second.source = advisor::SOURCE_RULES;
    second.issued_stardate = -5;
    second.objective = advisor::OBJECTIVE_KEEP;
    second.main_rival = advisor::NO_PLAYER;
    advisor::Set_Directive(0, &first);
    advisor::Set_Directive(MAX_PLAYERS - 1, &second);
    advisor::Set_Last_Objective_Change(0, 35012);
    advisor::Set_Last_Objective_Change(MAX_PLAYERS - 1, -3);

    const std::vector<uint8_t> bytes = Saved_Bytes_();
    DOCTEST_CHECK(bytes[0] == 2);  // little-endian payload version
    DOCTEST_CHECK(bytes[1] == 0);

    advisor::Clear_All_Directives();
    DOCTEST_CHECK(advisor::Get_Directive(0)->source == advisor::SOURCE_NONE);
    DOCTEST_CHECK(advisor::Last_Objective_Change(0) == 0);
    std::string error;
    DOCTEST_REQUIRE(Load_(bytes, &error));
    DOCTEST_CHECK(Saved_Bytes_() == bytes);
    DOCTEST_CHECK(advisor::Last_Objective_Change(0) == 35012);
    DOCTEST_CHECK(advisor::Last_Objective_Change(MAX_PLAYERS - 1) == -3);
    DOCTEST_CHECK(advisor::Last_Objective_Change(1) == 0);
    DOCTEST_CHECK(advisor::Last_Objective_Change(MAX_PLAYERS) == 0);
    DOCTEST_CHECK(advisor::Get_Directive(0)->rule_id == 0xABCD);
    DOCTEST_CHECK(advisor::Get_Directive(0)->expires_stardate == 110);
    DOCTEST_CHECK(advisor::Get_Directive(MAX_PLAYERS - 1)->issued_stardate == -5);
    DOCTEST_CHECK(advisor::Get_Directive(MAX_PLAYERS - 1)->main_rival == advisor::NO_PLAYER);
    DOCTEST_CHECK(advisor::Get_Directive(1)->source == advisor::SOURCE_NONE);
}

DOCTEST_TEST_CASE("directive save payload rejects malformed input and leaves the store unchanged") {
    s_store_scope scope;
    const advisor::s_directive stored = Sample_Directive_();
    advisor::Set_Directive(1, &stored);
    const std::vector<uint8_t> good = Saved_Bytes_();

    advisor::Clear_All_Directives();
    advisor::s_directive keeper = Sample_Directive_();
    keeper.rule_id = 7;
    advisor::Set_Directive(2, &keeper);

    const auto check_rejected = [&](const std::vector<uint8_t>& bytes) {
        std::string error;
        DOCTEST_CHECK_FALSE(Load_(bytes, &error));
        DOCTEST_CHECK_FALSE(error.empty());
        DOCTEST_CHECK(advisor::Get_Directive(1)->source == advisor::SOURCE_NONE);
        DOCTEST_CHECK(advisor::Get_Directive(2)->rule_id == 7);
    };

    std::vector<uint8_t> bytes = good;
    bytes[0] = 3;  // payload version
    check_rejected(bytes);

    bytes = good;
    bytes[0] = 1;  // a v1 payload has no last-change tail
    check_rejected(bytes);

    bytes = good;
    bytes.push_back(0);  // trailing byte
    check_rejected(bytes);

    bytes = good;
    bytes.pop_back();  // truncated
    check_rejected(bytes);

    check_rejected(std::vector<uint8_t>{});

    // Player 1's record starts after the version and one record; its first byte is the source.
    const size_t record_size = (good.size() - sizeof(uint16_t) - MAX_PLAYERS * sizeof(int32_t)) / MAX_PLAYERS;
    bytes = good;
    bytes[sizeof(uint16_t) + record_size] = 4;  // source out of range
    check_rejected(bytes);

    bytes = good;
    bytes[sizeof(uint16_t) + record_size + 10] = advisor::POSTURE_COUNT;  // after source, 2 x int32, objective
    check_rejected(bytes);
}

DOCTEST_TEST_CASE("a version 1 directive payload is read with no objective changes") {
    s_store_scope scope;
    const advisor::s_directive stored = Sample_Directive_();
    advisor::Set_Directive(1, &stored);
    advisor::Set_Last_Objective_Change(1, 35012);
    std::vector<uint8_t> bytes = Saved_Bytes_();
    bytes.resize(bytes.size() - MAX_PLAYERS * sizeof(int32_t));  // drop the v2 tail
    bytes[0] = 1;

    advisor::Clear_All_Directives();
    advisor::Set_Last_Objective_Change(2, 99);
    std::string error;
    DOCTEST_REQUIRE(Load_(bytes, &error));
    DOCTEST_CHECK(advisor::Get_Directive(1)->rule_id == 0xABCD);
    for (uint8_t player = 0; player < MAX_PLAYERS; ++player) {
        DOCTEST_CHECK(advisor::Last_Objective_Change(player) == 0);
    }
}
