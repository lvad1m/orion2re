#include "pch.h"

#include <string>

#include "doctest/doctest.h"
#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/advisor_sources.h"
#include "game/advisor_view.h"
#include "game/orion2_consts.h"

namespace {
    using namespace advisor;

    struct s_period_scope {
        int32_t saved = rules_period;
        s_period_scope() {
            rules_period = 5;
        }
        ~s_period_scope() {
            rules_period = saved;
        }
    };

    // A quiet empire: no contacts, enough colonies, enough explored stars, no famine.
    s_empire_view Quiet_View_(uint8_t personality = PLAYER_PERSONALITY_HONORABLE,
        uint8_t objective = PLAYER_OBJECTIVE_INDUSTRIALIST) {
        s_empire_view v;
        v.self.player = 0;
        v.self.objective = objective;
        v.self.personality = personality;
        v.self.stardate = 35100;  // turn 100: past the early-game guard of rule 105
        v.self.food_surplus = 10;
        v.self.colonies = 12;
        v.self.stars_explored = 40;
        v.self.stars_total = 60;
        v.self.fleet_strength = 5000;
        return v;
    }

    s_view_contact& Add_Contact_(s_empire_view* v, uint8_t player, int32_t treaty = 0, int32_t relations = 0) {
        s_view_contact& c = v->contacts[v->contact_count++];
        c.player = player;
        c.treaty.value = treaty;
        c.relations.value = relations;
        return c;
    }

    // A war that is not losing: weak enemy, few losses.
    s_view_contact& Add_Easy_War_(s_empire_view* v, uint8_t player) {
        s_view_contact& c = Add_Contact_(v, player, 5, -50);
        c.visible_fleet_strength.value = 1000;
        c.our_ship_losses.value = 1;
        c.their_ship_losses.value = 3;
        return c;
    }

    bool Has_(const std::string& note, const char* what) {
        return note.find(what) != std::string::npos;
    }
} // namespace

TEST_CASE("rules: nothing fires on a quiet empire") {
    s_period_scope period;
    s_empire_view v = Quiet_View_();
    s_directive d;
    d.rule_id = 77;
    std::string note = "x";
    CHECK_FALSE(Rules_Directive(&v, &d, &note));
    CHECK(d.source == SOURCE_NONE);
    CHECK(d.rule_id == 0);
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(note.empty());
}

TEST_CASE("rules: 101 famine") {
    s_period_scope period;
    s_empire_view v = Quiet_View_();
    v.self.food_surplus = -3;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.source == SOURCE_RULES);
    CHECK(d.rule_id == 101);
    CHECK(d.objective == PLAYER_OBJECTIVE_ECOLOGIST);
    CHECK(d.posture == POSTURE_KEEP);
    CHECK(d.main_rival == NO_PLAYER);
    CHECK(d.spy_target == NO_PLAYER);
    CHECK(d.spy_mission == SPY_KEEP);
    CHECK(d.research_focus == RESEARCH_KEEP);
    CHECK(d.fleet_doctrine == DOCTRINE_KEEP);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        CHECK(d.attitude[i] == ATTITUDE_KEEP);
    }
    for (int i = 0; i < WEIGHT_COUNT; ++i) {
        CHECK(d.weights[i] == LEVEL_KEEP);
    }
    CHECK(Has_(note, "rule 101"));
}

TEST_CASE("rules: 102 losing war by strength") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_ERRATIC);
    s_view_contact& c = Add_Contact_(&v, 2, 4, -80);
    c.visible_fleet_strength.value = 7501;  // > 1.5 * 5000
    c.our_ship_losses.value = 0;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.objective == PLAYER_OBJECTIVE_MILITARIST);
    CHECK(d.posture == POSTURE_DEFEND);
    CHECK(d.main_rival == 2);
    CHECK(Has_(note, "rule 102"));
    CHECK(Has_(note, "7501"));

    // Exactly 1.5x is not enough.
    c.visible_fleet_strength.value = 7500;
    c.their_ship_losses.value = 1;
    c.our_ship_losses.value = 1;
    s_directive d2;
    CHECK(Rules_Directive(&v, &d2, &note));  // rule 103 (erratic) instead
    CHECK(d2.rule_id == 103);
}

TEST_CASE("rules: 102 losing war by losses") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE);
    s_view_contact& c = Add_Contact_(&v, 1, 6, -80);
    c.visible_fleet_strength.value = 100;
    c.our_ship_losses.value = 2;   // >= 2 * max(1, 0)
    c.their_ship_losses.value = 0;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.main_rival == 1);
    CHECK(Has_(note, "losses 2/0"));

    c.our_ship_losses.value = 1;  // 1 < 2
    CHECK_FALSE(Rules_Directive(&v, &d, &note));  // honorable: no 103, no spies
    CHECK(d.source == SOURCE_NONE);
}

TEST_CASE("rules: 102 pacifist variant") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_PACIFIST, PLAYER_OBJECTIVE_MILITARIST);
    s_view_contact& c = Add_Contact_(&v, 3, 5, -80);
    c.visible_fleet_strength.value = 90000;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.objective == PLAYER_OBJECTIVE_DIPLOMATIC);
    CHECK(d.posture == POSTURE_MAKE_PEACE);
    CHECK(d.main_rival == 3);
}

TEST_CASE("rules: 102 picks the main enemy among losing wars, ties by lower player") {
    s_period_scope period;
    s_empire_view v = Quiet_View_();
    s_view_contact& a = Add_Contact_(&v, 4, 4, -80);
    a.visible_fleet_strength.value = 99999;
    a.our_colony_losses.value = 1;  // grievance 10
    s_view_contact& b = Add_Contact_(&v, 2, 4, -80);
    b.visible_fleet_strength.value = 99999;
    b.our_ship_losses.value = 10;   // grievance 10, lower player number
    s_view_contact& e = Add_Contact_(&v, 1, 4, -80);  // not losing, but highest grievance
    e.our_colony_losses.value = 5;
    e.their_ship_losses.value = 99;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.main_rival == 2);
}

TEST_CASE("rules: precedence, famine beats losing war") {
    s_period_scope period;
    s_empire_view v = Quiet_View_();
    v.self.food_surplus = -3;
    s_view_contact& c = Add_Contact_(&v, 2, 4, -80);
    c.visible_fleet_strength.value = 99999;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 101);
    CHECK(d.objective == PLAYER_OBJECTIVE_ECOLOGIST);
    CHECK(d.posture == POSTURE_KEEP);
    CHECK(d.main_rival == NO_PLAYER);
    CHECK_FALSE(Has_(note, "rule 102"));
}

TEST_CASE("rules: 103 at war, every qualifying personality") {
    s_period_scope period;
    const uint8_t personalities[] = {PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_PERSONALITY_RUTHLESS,
        PLAYER_PERSONALITY_XENOPHOBIC, PLAYER_PERSONALITY_ERRATIC};
    for (uint8_t p : personalities) {
        CAPTURE(static_cast<int>(p));
        s_empire_view v = Quiet_View_(p, PLAYER_OBJECTIVE_DIPLOMATIC);
        Add_Easy_War_(&v, 1);
        s_view_contact& hard = Add_Easy_War_(&v, 3);
        hard.our_colony_losses.value = 1;
        s_directive d;
        std::string note;
        REQUIRE(Rules_Directive(&v, &d, &note));
        CHECK(d.rule_id == 103);
        CHECK(d.objective == PLAYER_OBJECTIVE_MILITARIST);
        CHECK(d.posture == POSTURE_ATTACK_CURRENT_ENEMIES);
        CHECK(d.main_rival == 3);
        CHECK(Has_(note, "rule 103"));
    }
}

TEST_CASE("rules: 103 does not fire for other personalities or a MILITARIST empire") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    Add_Easy_War_(&v, 1);
    s_directive d;
    std::string note;
    CHECK_FALSE(Rules_Directive(&v, &d, &note));

    s_empire_view m = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    Add_Easy_War_(&m, 1);
    CHECK_FALSE(Rules_Directive(&m, &d, &note));
}

TEST_CASE("rules: 301 expand raises the colonization weight") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    v.self.free_planets_in_reach = 3;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 301);
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_HIGH);
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(Has_(note, "rule 301"));
    CHECK(Has_(note, "3 free planets in reach"));

    // Nothing known to colonize: no directive in a quiet galaxy.
    v.self.free_planets_in_reach = 0;
    CHECK_FALSE(Rules_Directive(&v, &d, &note));
}

TEST_CASE("rules: 301 stays off while losing a war and rides along other rules") {
    s_period_scope period;
    s_directive d;
    std::string note;

    s_empire_view losing = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    losing.self.free_planets_in_reach = 3;
    s_view_contact& enemy = Add_Contact_(&losing, 1, 5, -80);
    enemy.visible_fleet_strength.value = 10000;  // more than 1.5 x our 5000
    REQUIRE(Rules_Directive(&losing, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_KEEP);
    CHECK_FALSE(Has_(note, "rule 301"));

    // Famine (101) still lets the empire expand; the objective rule keeps rule_id.
    s_empire_view hungry = Quiet_View_();
    hungry.self.food_surplus = -5;
    hungry.self.free_planets_in_reach = 2;
    REQUIRE(Rules_Directive(&hungry, &d, &note));
    CHECK(d.rule_id == 101);
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_HIGH);
}

TEST_CASE("rules: stagnation no longer switches the objective to EXPANSIONIST (rule 104 retired)") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    v.self.stardate = 35060;
    v.self.colonies = 2;
    v.self.stars_explored = 4;
    v.self.stars_total = 36;
    v.self.free_planets_in_reach = 5;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 301);
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK_FALSE(Has_(note, "rule 104"));
}

TEST_CASE("rules: 105 peace dividend and its personality variants") {
    s_period_scope period;
    struct s_case {
        uint8_t personality;
        uint8_t expected;
    };
    const s_case cases[] = {
        {PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_TECHNOLOGIST},
        {PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC},
        {PLAYER_PERSONALITY_PACIFIST, PLAYER_OBJECTIVE_DIPLOMATIC},
        {PLAYER_PERSONALITY_RUTHLESS, PLAYER_OBJECTIVE_INDUSTRIALIST},
    };
    for (const s_case& tc : cases) {
        CAPTURE(static_cast<int>(tc.personality));
        s_empire_view v = Quiet_View_(tc.personality, PLAYER_OBJECTIVE_MILITARIST);
        Add_Contact_(&v, 1, 0, -25);
        Add_Contact_(&v, 2, 0, 40);
        s_directive d;
        std::string note;
        REQUIRE(Rules_Directive(&v, &d, &note));
        CHECK(d.rule_id == 105);
        CHECK(d.objective == tc.expected);
        CHECK(Has_(note, "rule 105"));
    }

    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    Add_Contact_(&v, 1, 0, -26);
    s_directive d;
    std::string note;
    CHECK_FALSE(Rules_Directive(&v, &d, &note));

    s_empire_view n = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    Add_Contact_(&n, 1, 0, 0);
    CHECK_FALSE(Rules_Directive(&n, &d, &note));
}

TEST_CASE("rules: 201 spy war and its personality variants") {
    s_period_scope period;
    for (uint8_t p : {static_cast<uint8_t>(PLAYER_PERSONALITY_AGGRESSIVE),
             static_cast<uint8_t>(PLAYER_PERSONALITY_HONORABLE), static_cast<uint8_t>(PLAYER_PERSONALITY_PACIFIST)}) {
        CAPTURE(static_cast<int>(p));
        // Not a losing war, objective already MILITARIST: only the spy rule fires.
        s_empire_view v = Quiet_View_(p, PLAYER_OBJECTIVE_MILITARIST);
        Add_Easy_War_(&v, 1).our_spies = 0;
        s_view_contact& w = Add_Easy_War_(&v, 2);
        w.our_spies = 3;
        s_directive d;
        std::string note;
        REQUIRE(Rules_Directive(&v, &d, &note));
        CHECK(d.rule_id == 201);
        CHECK(d.objective == OBJECTIVE_KEEP);
        CHECK(d.spy_target == 2);
        const bool soft = p != PLAYER_PERSONALITY_AGGRESSIVE;
        CHECK(d.spy_mission == (soft ? SPY_STEAL_TECH : SPY_SABOTAGE));
        CHECK(Has_(note, "rule 201"));
        CHECK(Has_(note, "3 spies"));
    }
}

TEST_CASE("rules: 201 prefers the main enemy among several with spies, ties by lower player") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    s_view_contact& a = Add_Easy_War_(&v, 3);
    a.our_spies = 1;
    a.our_colony_losses.value = 2;
    s_view_contact& b = Add_Easy_War_(&v, 1);
    b.our_spies = 1;
    b.our_colony_losses.value = 2;
    s_view_contact& c = Add_Easy_War_(&v, 0 + 2);
    c.our_spies = 0;
    c.our_colony_losses.value = 9;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.spy_target == 1);
}

TEST_CASE("rules: 202 spy rival, lowest relations, ties by lower player") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    Add_Contact_(&v, 1, 0, -90).our_spies = 0;  // lowest but no spies
    Add_Contact_(&v, 4, 0, -10).our_spies = 2;
    Add_Contact_(&v, 3, 0, -10).our_spies = 1;
    Add_Contact_(&v, 2, 0, 30).our_spies = 1;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 202);
    CHECK(d.spy_target == 3);
    CHECK(d.spy_mission == SPY_STEAL_TECH);
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(Has_(note, "rule 202"));
}

TEST_CASE("rules: objective equal to current is OBJECTIVE_KEEP, spy fields still carried") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_ECOLOGIST);
    v.self.food_surplus = -5;
    Add_Contact_(&v, 2, 0, -50).our_spies = 2;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(d.rule_id == 101);  // objective rule id wins
    CHECK(d.spy_target == 2);
    CHECK(d.spy_mission == SPY_STEAL_TECH);
    CHECK(Has_(note, "rule 101"));
    CHECK(Has_(note, "rule 202"));
}

TEST_CASE("rules: expiry is stardate + rules_period - 1, clamped") {
    s_period_scope period;
    s_empire_view v = Quiet_View_();
    v.self.food_surplus = -3;
    v.self.stardate = 250;
    s_directive d;
    std::string note;

    rules_period = 5;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.issued_stardate == 250);
    CHECK(d.expires_stardate == 254);

    rules_period = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.expires_stardate == 250);

    rules_period = 0;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.expires_stardate == 250);
}

TEST_CASE("rules: deterministic note and cleared output on no rule") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_ERRATIC);
    s_view_contact& c = Add_Contact_(&v, 2, 4, -80);
    c.visible_fleet_strength.value = 12500;
    c.our_ship_losses.value = 21;
    c.their_ship_losses.value = 6;
    c.our_spies = 3;
    v.self.fleet_strength = 4100;
    s_directive d1;
    s_directive d2;
    std::string n1;
    std::string n2;
    REQUIRE(Rules_Directive(&v, &d1, &n1));
    REQUIRE(Rules_Directive(&v, &d2, &n2));
    CHECK(n1 == n2);
    CHECK(n1 == "rule 102 losing_war: player 2 visible 12500 vs ours 4100, losses 21/6 -> MILITARIST, rival 2; "
                "rule 201 spy_war: player 2, 3 spies -> SABOTAGE");

    s_empire_view quiet = Quiet_View_();
    CHECK_FALSE(Rules_Directive(&quiet, &d1, &n1));
    CHECK(d1.source == SOURCE_NONE);
}

TEST_CASE("rules: early-game and noise guards (self-play findings)") {
    s_period_scope period;
    s_directive d;
    std::string note;

    // A deficit of 1-2 food is routine: no famine.
    s_empire_view f = Quiet_View_();
    f.self.food_surplus = -2;
    CHECK_FALSE(Rules_Directive(&f, &d, &note));

    // A militarist with no contacts is not "at peace": no peace dividend.
    s_empire_view lonely = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    CHECK_FALSE(Rules_Directive(&lonely, &d, &note));

    // Before turn 50 the rolled MILITARIST objective stays even with peaceful contacts.
    s_empire_view early = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    early.self.stardate = 35049;
    Add_Contact_(&early, 1, 0, 50);
    CHECK_FALSE(Rules_Directive(&early, &d, &note));
    early.self.stardate = 35050;
    REQUIRE(Rules_Directive(&early, &d, &note));
    CHECK(d.rule_id == 105);
}
