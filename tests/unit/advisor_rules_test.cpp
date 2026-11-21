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

    // With contacts but no other rule, only rule 203 fires: it names a spy target and keeps the
    // objective.
    bool Only_Spy_Build_(const s_empire_view* v) {
        s_directive d;
        std::string note;
        return Rules_Directive(v, &d, &note) && d.rule_id == 203 && d.objective == OBJECTIVE_KEEP &&
            d.spy_target != NO_PLAYER && d.weights[WEIGHT_COLONIZATION] == LEVEL_KEEP;
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
    // Byzantine doctrine (default on): seek peace when losing; DEFEND without it.
    CHECK(d.posture == (rules_doctrine ? POSTURE_MAKE_PEACE : POSTURE_DEFEND));
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
    CHECK(Only_Spy_Build_(&v));  // honorable: no 103; no spies yet, so rule 203
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
    CHECK(Only_Spy_Build_(&v));

    s_empire_view m = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    Add_Easy_War_(&m, 1);
    CHECK(Only_Spy_Build_(&m));
}

TEST_CASE("rules: 301 expand raises the colonization weight") {
    s_period_scope period;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    v.self.free_planets_in_reach = 3;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 301);
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_MAX);  // three or more: two colony ships at once
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(Has_(note, "rule 301"));
    CHECK(Has_(note, "3 free planets and 0 outpost sites in reach -> colonization MAX"));

    v.self.free_planets_in_reach = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_HIGH);
    v.self.free_planets_in_reach = 3;

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
    CHECK(Only_Spy_Build_(&v));

    s_empire_view n = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    Add_Contact_(&n, 1, 0, 0);
    CHECK(Only_Spy_Build_(&n));
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
    CHECK(Only_Spy_Build_(&early));
    early.self.stardate = 35050;
    REQUIRE(Rules_Directive(&early, &d, &note));
    CHECK(d.rule_id == 105);
}

TEST_CASE("rules: 203 names a spy target to build spies for") {
    s_period_scope period;
    s_directive d;
    std::string note;

    // At war, no spies anywhere: the main enemy; sabotage unless honorable or pacifist.
    for (uint8_t p : {static_cast<uint8_t>(PLAYER_PERSONALITY_AGGRESSIVE), static_cast<uint8_t>(PLAYER_PERSONALITY_HONORABLE)}) {
        CAPTURE(static_cast<int>(p));
        s_empire_view v = Quiet_View_(p, PLAYER_OBJECTIVE_MILITARIST);
        Add_Easy_War_(&v, 1);
        Add_Easy_War_(&v, 3).our_colony_losses.value = 1;
        REQUIRE(Rules_Directive(&v, &d, &note));
        CHECK(d.rule_id == 203);
        CHECK(d.spy_target == 3);
        CHECK(d.spy_mission == (p == PLAYER_PERSONALITY_HONORABLE ? SPY_STEAL_TECH : SPY_SABOTAGE));
        CHECK(Has_(note, "rule 203 spy_build: player 3 (war), no spies there"));
    }

    // At peace: the lowest relations, ties by lower player; always steal technology.
    s_empire_view peace = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    Add_Contact_(&peace, 4, 0, -40);
    Add_Contact_(&peace, 2, 0, -40);
    Add_Contact_(&peace, 1, 0, 30);
    REQUIRE(Rules_Directive(&peace, &d, &note));
    CHECK(d.rule_id == 203);
    CHECK(d.spy_target == 2);
    CHECK(d.spy_mission == SPY_STEAL_TECH);

    // With spies at a contact, rule 202 steers them and 203 stays quiet.
    peace.contacts[2].our_spies = 1;
    REQUIRE(Rules_Directive(&peace, &d, &note));
    CHECK(d.rule_id == 202);
    CHECK(d.spy_target == 1);
    CHECK_FALSE(Has_(note, "rule 203"));
}

TEST_CASE("rules: 401 balance of power turns against a clear leader") {
    s_period_scope period;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    v.self.colonies = 4;
    v.self.fleet_strength = 1000;
    s_view_contact& big = Add_Contact_(&v, 3, 0, 10);
    big.known_colonies.value = 14;
    big.visible_fleet_strength.value = 6000;
    s_view_contact& small = Add_Contact_(&v, 2, 0, 10);
    small.known_colonies.value = 3;
    small.visible_fleet_strength.value = 1000;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[3] == ATTITUDE_HOSTILE);
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.main_rival == 3);
    CHECK(Has_(note, "rule 401 balance: player 3 leads"));

    // An ally stays an ally: no hostility, the others still become friends.
    big.treaty.value = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[3] == ATTITUDE_KEEP);
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);

    // We lead: nothing to balance.
    big.treaty.value = 0;
    v.self.colonies = 30;
    v.self.fleet_strength = 20000;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[3] == ATTITUDE_KEEP);
    CHECK_FALSE(Has_(note, "rule 401"));

    // A close race is not a runaway leader.
    v.self.colonies = 12;
    v.self.fleet_strength = 5000;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK_FALSE(Has_(note, "rule 401"));
}

TEST_CASE("rules: 102 weighs warship numbers by Lanchester's square law") {
    s_period_scope period;
    const bool lanchester = rules_lanchester;
    s_directive d;
    std::string note;
    // Equal total strength, but they have ten warships to our two: under the square law we lose.
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_DIPLOMATIC);
    v.self.fleet_strength = 5000;
    v.self.combat_ships = 2;
    s_view_contact& c = Add_Contact_(&v, 1, 5, -60);
    c.visible_fleet_strength.value = 5000;
    c.visible_combat_ships.value = 10;
    rules_lanchester = true;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    rules_lanchester = false;  // linear strength: an even fight
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id != 102);
    rules_lanchester = lanchester;
}

TEST_CASE("rules: 501 pushes range research while most of the galaxy is unexplored") {
    s_period_scope period;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_();
    v.self.stardate = 35050;  // turn 50: early game
    v.self.stars_explored = 5;
    v.self.stars_total = 36;
    v.self.free_planets_in_reach = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.research_focus == RESEARCH_PROPULSION);
    CHECK(Has_(note, "rule 501 explore: 5 of 36 stars explored, 1 free planets in reach"));

    v.self.free_planets_in_reach = 3;  // free planets in reach do not matter
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.research_focus == RESEARCH_PROPULSION);

    v.self.free_planets_in_reach = 0;
    v.self.stars_explored = 18;  // half explored
    CHECK_FALSE(Rules_Directive(&v, &d, &note));

    v.self.stars_explored = 5;
    v.self.stardate = 35100;  // turn 100: the early game is over
    CHECK_FALSE(Rules_Directive(&v, &d, &note));
}

TEST_CASE("rules: 301 also fires for outpost sites alone") {
    s_period_scope period;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_();
    v.self.free_outpost_sites_in_reach = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 301);
    CHECK(d.weights[WEIGHT_COLONIZATION] == LEVEL_HIGH);
    CHECK(Has_(note, "0 free planets and 2 outpost sites in reach"));
}

TEST_CASE("rules: 402 befriends the enemies of our enemy (Byzantine doctrine)") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    rules_doctrine = true;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_HONORABLE, PLAYER_OBJECTIVE_MILITARIST);
    Add_Easy_War_(&v, 1);                               // our enemy
    Add_Contact_(&v, 2, 0, 0).at_war_with = 1u << 1;    // also fights player 1
    Add_Contact_(&v, 3, 0, 0).at_war_with = 1u << 4;    // fights someone else
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_ALLY);
    CHECK(d.attitude[3] == ATTITUDE_KEEP);
    CHECK(Has_(note, "rule 402 enemy_of_my_enemy: 1 contacts"));
    rules_doctrine = false;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    rules_doctrine = doctrine;
}

TEST_CASE("rules: losing a war seeks peace for every personality under the doctrine") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_DIPLOMATIC);
    s_view_contact& enemy = Add_Contact_(&v, 1, 5, -80);
    enemy.visible_fleet_strength.value = 10000;
    rules_doctrine = true;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.rule_id == 102);
    CHECK(d.posture == POSTURE_MAKE_PEACE);
    rules_doctrine = false;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.posture == POSTURE_DEFEND);
    rules_doctrine = doctrine;
}

TEST_CASE("rules: 403 spares a beaten enemy that does not lead (Byzantine doctrine)") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    rules_doctrine = true;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    v.self.fleet_strength = 9000;
    v.self.combat_ships = 8;
    s_view_contact& weak = Add_Easy_War_(&v, 1);
    weak.known_colonies.value = 2;
    weak.visible_fleet_strength.value = 1000;
    weak.visible_combat_ships.value = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_FRIENDLY);
    CHECK(Has_(note, "rule 403 spare: player 1"));

    weak.known_colonies.value = 5;  // not beaten yet
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] != ATTITUDE_FRIENDLY);
    rules_doctrine = doctrine;
}

TEST_CASE("rules: 105 keeps the fleet while a neighbour is stronger (Byzantine rule I)") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    const int32_t parts = doctrine_parts;
    rules_doctrine = true;
    doctrine_parts = DOCTRINE_PART_READINESS;  // off by default (61)
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    v.self.stardate = 35100;
    v.self.fleet_strength = 1000;
    v.self.combat_ships = 4;
    s_view_contact& c = v.contacts[v.contact_count++];
    c.player = 2;
    c.relations.value = 10;
    c.visible_fleet_strength.value = 4000;
    c.visible_combat_ships.value = 8;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.objective == OBJECTIVE_KEEP);
    CHECK(Has_(note, "rule 105 peace_dividend withheld"));

    c.visible_fleet_strength.value = 500;  // we are the stronger one: the dividend is paid
    c.visible_combat_ships.value = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.objective != OBJECTIVE_KEEP);
    CHECK(Has_(note, "rule 105 peace_dividend:"));
    rules_doctrine = doctrine;
    doctrine_parts = parts;
}

TEST_CASE("rules: 404 prefers peace only with neighbours costly to fight") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    const int32_t parts = doctrine_parts;
    rules_doctrine = true;
    doctrine_parts = DOCTRINE_PART_ALL;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
    v.self.fleet_strength = 3000;
    v.self.combat_ships = 2;                        // fighting power 9000
    s_view_contact& strong = Add_Contact_(&v, 1, 0, 0);
    strong.visible_fleet_strength.value = 3000;     // 6000: two thirds -> costly
    strong.visible_combat_ships.value = 1;
    s_view_contact& weak = Add_Contact_(&v, 2, 0, 0);
    weak.visible_fleet_strength.value = 2000;       // 4000: clearly weaker
    weak.visible_combat_ships.value = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_NEUTRAL);
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    CHECK(Has_(note, "rule 404 peace_from_strength: 1 neighbours"));

    doctrine_parts = DOCTRINE_PART_ALL & ~DOCTRINE_PART_PEACE;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_KEEP);
    rules_doctrine = doctrine;
    doctrine_parts = parts;
}

TEST_CASE("rules: 402 courts the allies of our enemies") {
    s_period_scope period;
    const bool doctrine = rules_doctrine;
    const int32_t parts = doctrine_parts;
    rules_doctrine = true;
    doctrine_parts = DOCTRINE_PART_ALLIANCES;
    s_directive d;
    std::string note;
    s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_MILITARIST);
    Add_Easy_War_(&v, 1);
    Add_Contact_(&v, 2, 0, 0).allied_with = 1u << 1;   // allied with our enemy
    Add_Contact_(&v, 3, 0, 0).at_war_with = 1u << 1;   // fights our enemy
    Add_Contact_(&v, 4, 0, 0);                         // neither
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.attitude[3] == ATTITUDE_ALLY);
    CHECK(d.attitude[4] == ATTITUDE_KEEP);
    CHECK(Has_(note, "1 contacts fight our enemies -> ALLY, 1 allies of our enemies -> FRIENDLY"));
    rules_doctrine = doctrine;
    doctrine_parts = parts;
}

namespace {
    // Rule 405 tests: only the third-party part (plus `extra`), Lanchester on; we have fighting
    // power 3000 x (2 + 1) = 9000 and 12 colonies.
    struct s_third_party_scope {
        s_period_scope period;
        bool doctrine = rules_doctrine;
        int32_t parts = doctrine_parts;
        bool lanchester = rules_lanchester;
        explicit s_third_party_scope(int32_t extra = 0) {
            rules_doctrine = true;
            doctrine_parts = DOCTRINE_PART_THIRD_PARTY | extra;
            rules_lanchester = true;
        }
        ~s_third_party_scope() {
            rules_doctrine = doctrine;
            doctrine_parts = parts;
            rules_lanchester = lanchester;
        }
    };

    s_empire_view Third_Party_View_() {
        s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
        v.self.fleet_strength = 3000;
        v.self.combat_ships = 2;
        v.self.free_planets_in_reach = 0;
        return v;
    }

    // A contact with fighting power strength x (ships + 1), known through one of our spies.
    s_view_contact& Add_Power_(s_empire_view* v, uint8_t player, int32_t strength, int32_t ships, int32_t treaty = 0) {
        s_view_contact& c = Add_Contact_(v, player, treaty, 0);
        c.visible_fleet_strength.value = strength;
        c.visible_combat_ships.value = ships;
        c.known_colonies.value = 3;
        c.our_spies = 1;
        c.in_range = 1;
        return c;
    }

    void Fight_(s_view_contact* a, s_view_contact* b) {
        a->at_war_with = static_cast<uint8_t>(a->at_war_with | (1u << b->player));
        b->at_war_with = static_cast<uint8_t>(b->at_war_with | (1u << a->player));
    }
} // namespace

TEST_CASE("rules: 405 protects a weak victim of an attacker dangerous to us") {
    s_third_party_scope scope;
    s_empire_view v = Third_Party_View_();
    s_view_contact& attacker = Add_Power_(&v, 1, 4000, 2);  // 12000: dangerous to us
    s_view_contact& victim = Add_Power_(&v, 2, 1000, 1);    // 2000: no danger to us
    Fight_(&attacker, &victim);
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.attitude[1] == ATTITUDE_WARY);
    CHECK(d.main_rival == 1);
    CHECK(Has_(note, "rule 405 third_party: protect 1"));

    // A pact with the attacker is kept: no wariness toward a partner.
    v.contacts[0].treaty.value = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.attitude[1] == ATTITUDE_KEEP);

    // About equal: stay out.
    v.contacts[0].treaty.value = 0;
    v.contacts[1].visible_fleet_strength.value = 4500;  // 9000 against 12000: more than 2/3
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_KEEP);
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    CHECK_FALSE(Has_(note, "rule 405"));
}

TEST_CASE("rules: 405 leaves a war between equals alone") {
    s_third_party_scope scope;
    s_empire_view v = Third_Party_View_();
    s_view_contact& a = Add_Power_(&v, 1, 1000, 1);  // 2000 each: both weak for us
    s_view_contact& b = Add_Power_(&v, 2, 1000, 1);
    Fight_(&a, &b);
    s_directive d;
    std::string note;
    Rules_Directive(&v, &d, &note);
    CHECK(d.attitude[1] == ATTITUDE_KEEP);
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    CHECK_FALSE(Has_(note, "rule 405"));
}

TEST_CASE("rules: 405 joins a war on a weak neighbour only with a calm rear and known strength") {
    s_third_party_scope scope;
    s_empire_view v = Third_Party_View_();
    s_view_contact& attacker = Add_Power_(&v, 1, 1500, 2);  // 4500: no danger to us
    s_view_contact& victim = Add_Power_(&v, 2, 1000, 1);    // 2000: weak for us, losing to 4500
    Fight_(&attacker, &victim);
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_HOSTILE);
    CHECK(d.attitude[1] == ATTITUDE_KEEP);
    CHECK(Has_(note, "join 1 wars on weak neighbours"));

    v.contacts[1].our_spies = 0;  // strength unknown: no war on a guess
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    v.contacts[1].our_spies = 1;

    s_view_contact& giant = Add_Power_(&v, 3, 5000, 2);  // 15000: dangerous, not bound -> rear unsafe
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    giant.treaty.value = 1;  // a pact with the giant calms the rear
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_HOSTILE);
    v.contacts[1].allied_with = 1u << 3;  // ...unless the victim is allied with the giant
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    v.contacts[1].allied_with = 0;

    Add_Easy_War_(&v, 4);  // one front at a time
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
}

TEST_CASE("rules: 405 preys on a weak isolated neighbour once peaceful expansion is over") {
    s_third_party_scope scope;
    s_empire_view v = Third_Party_View_();
    Add_Power_(&v, 2, 1000, 1);
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_HOSTILE);
    CHECK(Has_(note, "prey on 1 (HOSTILE)"));

    v.self.free_planets_in_reach = 2;  // colonize first
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    v.self.free_planets_in_reach = 0;

    v.contacts[0].in_range = 0;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    v.contacts[0].in_range = 1;

    v.contacts[0].treaty.value = 2;  // an ally is never prey
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
    v.contacts[0].treaty.value = 0;

    doctrine_parts = DOCTRINE_PART_DEFAULT;  // the default leaves the part out
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_KEEP);
}

TEST_CASE("rules: with the third-party part 403 spares a beaten enemy only as a counterweight") {
    s_third_party_scope scope(DOCTRINE_PART_SPARE);
    s_empire_view v = Third_Party_View_();
    s_view_contact& beaten = Add_Power_(&v, 2, 100, 1, 5);  // at war with us, 200 power
    beaten.known_colonies.value = 1;
    s_directive d;
    std::string note;
    Rules_Directive(&v, &d, &note);
    CHECK(d.attitude[2] == ATTITUDE_KEEP);  // nobody else to balance: finish it
    CHECK_FALSE(Has_(note, "rule 403"));

    Add_Power_(&v, 3, 5000, 2);  // a dangerous neighbour: the beaten enemy is a counterweight
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(Has_(note, "rule 403 spare: player 2"));

    doctrine_parts = DOCTRINE_PART_SPARE;  // without the part: always spared
    v.contact_count = 1;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
}

namespace {
    // Phase strategy tests: rules_phases on, the doctrine off (its rules would also set attitudes).
    struct s_phase_scope {
        s_period_scope period;
        bool phases = rules_phases;
        int32_t parts = phase_parts;
        bool doctrine = rules_doctrine;
        bool lanchester = rules_lanchester;
        s_phase_scope() {
            rules_phases = true;
            phase_parts = PHASE_PART_ALL;
            rules_doctrine = false;
            rules_lanchester = true;
        }
        ~s_phase_scope() {
            rules_phases = phases;
            phase_parts = parts;
            rules_doctrine = doctrine;
            rules_lanchester = lanchester;
        }
    };

    // Turn 150; we hold 12 colonies, population 100 (10 votes) and fighting power 3000 x 3 = 9000.
    s_empire_view Phase_View_() {
        s_empire_view v = Quiet_View_(PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_OBJECTIVE_INDUSTRIALIST);
        v.self.stardate = 35150;
        v.self.population = 100;
        v.self.fleet_strength = 3000;
        v.self.combat_ships = 2;
        return v;
    }

    s_view_contact& Add_Empire_(s_empire_view* v, uint8_t player, int32_t colonies, int32_t population,
        int32_t strength, int32_t ships, int32_t treaty = 0) {
        s_view_contact& c = Add_Contact_(v, player, treaty, 0);
        c.known_colonies.value = colonies;
        c.population.value = population;
        c.visible_fleet_strength.value = strength;
        c.visible_combat_ships.value = ships;
        c.our_spies = 1;
        c.in_range = 1;
        return c;
    }
} // namespace

TEST_CASE("rules: 406 a leader conquers one weak neighbour at a time and courts the rest for votes") {
    s_phase_scope scope;
    s_empire_view v = Phase_View_();
    Add_Empire_(&v, 1, 4, 40, 1500, 2);  // the strongest other empire (power 4500)
    Add_Empire_(&v, 2, 2, 20, 500, 1);   // the weakest neighbour (1000): the target
    Add_Empire_(&v, 3, 3, 30, 1000, 1);  // weak too (2000), but the most votes among the rest
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(Has_(note, "rule 406 leader: 10 of 19 known votes with our allies; conquer player 2; peace with 1; ally sought 3"));
    CHECK(d.attitude[2] == ATTITUDE_WAR);
    CHECK(d.posture == POSTURE_START_NEW_WAR);
    CHECK(d.main_rival == 2);
    CHECK(d.attitude[1] == ATTITUDE_NEUTRAL);
    CHECK(d.attitude[3] == ATTITUDE_ALLY);
    CHECK(d.weights[WEIGHT_FLEET] == LEVEL_HIGH);

    // An ally's votes bring us to two thirds: keep the peace and let the council decide.
    v.contacts[2].treaty.value = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(Has_(note, "13 of 19 known votes with our allies (two thirds: keep the peace)"));
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.posture == POSTURE_KEEP);
    v.contacts[2].treaty.value = 0;

    // A war of our own comes first: no new target; a weak enemy is fought to the end, a strong
    // one is left to the other rules.
    v.contacts[1].treaty.value = 5;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.posture == POSTURE_ATTACK_CURRENT_ENEMIES);
    CHECK(d.attitude[2] == ATTITUDE_WAR);
    CHECK(d.attitude[1] == ATTITUDE_NEUTRAL);  // peace with the strongest other empire
    v.contacts[1].treaty.value = 0;
    v.contacts[0].treaty.value = 5;
    v.contacts[0].visible_fleet_strength.value = 2000;  // 6000: not weak
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_KEEP);
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    v.contacts[0].treaty.value = 0;
    v.contacts[0].visible_fleet_strength.value = 1500;

    // An empire we know nothing about looks empty: no claim to lead, and it counts as large as us.
    s_empire_view blind = v;
    Add_Contact_(&blind, 4, 0, 0);
    Rules_Directive(&blind, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
    // No spies at one contact: known through a broad exploration (40 of 60 stars), but not after a
    // narrow one.
    s_empire_view unspied = v;
    unspied.contacts[0].our_spies = 0;
    Rules_Directive(&unspied, &d, &note);
    CHECK(Has_(note, "rule 406 leader"));
    unspied.self.stars_explored = 20;
    Rules_Directive(&unspied, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
    // A contact with more than 2/3 of our colonies: no claim to lead.
    s_empire_view close = v;
    close.contacts[2].known_colonies.value = 9;
    Rules_Directive(&close, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
    close.contacts[2].known_colonies.value = 8;
    Rules_Directive(&close, &d, &note);
    CHECK(Has_(note, "rule 406 leader"));
    // One contact is enough to lead; the leader part can be left out.
    s_empire_view alone = v;
    alone.contact_count = 1;
    Rules_Directive(&alone, &d, &note);
    CHECK(Has_(note, "rule 406 leader"));
    phase_parts = PHASE_PART_AGAINST_WINNER;
    Rules_Directive(&alone, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
    phase_parts = PHASE_PART_ALL;

    // Not before turn 100, and not with the flag off.
    v.self.stardate = 35050;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
    v.self.stardate = 35150;
    rules_phases = false;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 406"));
}

TEST_CASE("rules: 407 resists an empire nearing a council win and courts its allies") {
    s_phase_scope scope;
    s_empire_view v = Phase_View_();
    v.self.population = 80;  // 8 votes; we do not lead
    v.self.colonies = 4;
    s_view_contact& giant = Add_Empire_(&v, 1, 6, 120, 3000, 2);  // 12 votes
    s_view_contact& friend_of_giant = Add_Empire_(&v, 2, 3, 60, 1000, 1);  // 6 votes
    Add_Empire_(&v, 3, 2, 40, 1000, 1);  // 4 votes
    giant.allied_with = 1u << 2;
    friend_of_giant.allied_with = 1u << 1;
    s_directive d;
    std::string note;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(Has_(note, "rule 407 council_threat: player 1 has 18 of 30 known votes with its allies -> HOSTILE, court 1 of its allies"));
    CHECK(d.attitude[1] == ATTITUDE_HOSTILE);
    CHECK(d.attitude[2] == ATTITUDE_FRIENDLY);
    CHECK(d.main_rival == 1);

    // Our ally is the would-be winner: wary, not hostile (an alliance is kept).
    v.contacts[0].treaty.value = 2;
    REQUIRE(Rules_Directive(&v, &d, &note));
    CHECK(d.attitude[1] == ATTITUDE_WARY);

    // Only with the part on.
    phase_parts = PHASE_PART_LEADER;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 407"));
    phase_parts = PHASE_PART_ALL;

    // Over half but under 3/5 of the votes: nothing (16 of 28; at half it ganged up on every leader).
    v.contacts[0].population.value = 100;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 407"));
    v.contacts[0].population.value = 120;

    // Small numbers are noise: a would-be winner needs ten sure votes.
    v.contacts[0].treaty.value = 0;
    for (int i = 0; i < 3; ++i) v.contacts[i].population.value /= 10;
    v.self.population /= 10;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 407"));
    for (int i = 0; i < 3; ++i) v.contacts[i].population.value *= 10;
    v.self.population *= 10;

    // Under half of the votes: nothing.
    v.contacts[0].allied_with = 0;
    v.contacts[1].allied_with = 0;
    Rules_Directive(&v, &d, &note);
    CHECK_FALSE(Has_(note, "rule 407"));
}
