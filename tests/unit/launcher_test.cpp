#include "pch.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "config/cfg_parser.h"
#include "doctest/doctest.h"
#include "launcher_config.h"

namespace {
    config::s_cfg_document Parse_(const std::string& text) {
        config::s_cfg_document document;
        config::s_cfg_diagnostic diagnostic;
        const bool ok = config::Parse_Cfg("user.cfg", text.c_str(), text.size(), &document, &diagnostic);
        DOCTEST_INFO(diagnostic.message);
        DOCTEST_REQUIRE(ok);
        return document;
    }

    std::string Schema_() {
        std::ifstream in(CONFIG_MAIN_SCHEMA_PATH, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Mechanics_Text_() {
        std::ifstream in(LAUNCHER_MECHANICS_PATH, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // The schema block of one top-level parameter, "" when there is none.
    std::string Schema_Block_(const std::string& schema, const std::string& name) {
        const std::string head = "\n    " + name + ": {";
        const size_t begin = schema.find(head);
        if (begin == std::string::npos) return {};
        const size_t end = schema.find("\n    },", begin);
        return end == std::string::npos ? std::string() : schema.substr(begin, end - begin);
    }

    launcher::s_settings Settings_(launcher::e_preset preset) {
        launcher::s_settings settings;
        settings.preset = preset;
        return settings;
    }
} // namespace

DOCTEST_TEST_SUITE("launcher") {

DOCTEST_TEST_CASE("every preset writes a user.cfg that parses and names real config keys") {
    const std::string schema = Schema_();
    DOCTEST_REQUIRE(!schema.empty());
    for (int preset = 0; preset < launcher::PRESET_COUNT; ++preset) {
        launcher::s_settings settings = Settings_(static_cast<launcher::e_preset>(preset));
        settings.mechanics.espionage = 2;
        settings.mechanics.expansion = 2;
        const std::string text = launcher::Build_User_Cfg(settings);
        DOCTEST_CHECK(launcher::Is_Launcher_User_Cfg(text));
        const config::s_cfg_document document = Parse_(text);
        for (const config::s_cfg_statement& statement : document.statements) {
            if (statement.kind != config::CFG_STATEMENT_ASSIGNMENT) continue;
            DOCTEST_REQUIRE(statement.path.size() >= 2);
            const std::string key = statement.path[1].raw;
            DOCTEST_INFO("key " << statement.path[0].raw << " " << key);
            DOCTEST_CHECK(Has_(schema, ("    " + key + ": {").c_str()));
            DOCTEST_CHECK(Has_(schema, ("namespace: \"" + statement.path[0].raw + "\"").c_str()));
        }
    }
}

DOCTEST_TEST_CASE("every key of the mechanics list is a bool setting in the schema") {
    const std::string schema = Schema_();
    launcher::s_mechanics_list list;
    DOCTEST_REQUIRE(launcher::Parse_Mechanics_List(Mechanics_Text_(), &list));
    DOCTEST_CHECK(list.Count() > 40);
    for (const launcher::s_mechanic_category& category : list.categories) {
        DOCTEST_CHECK(!category.title.empty());
        for (const launcher::s_mechanic_ref& ref : category.settings) {
            DOCTEST_INFO("key " << ref.name_space << " " << ref.name);
            const std::string block = Schema_Block_(schema, ref.name);
            DOCTEST_REQUIRE(!block.empty());
            DOCTEST_CHECK(Has_(block, ("namespace: \"" + ref.name_space + "\"").c_str()));
            DOCTEST_CHECK(Has_(block, "type: \"bool\""));
            DOCTEST_CHECK(Has_(block, "default: false"));
        }
    }

    // Every box written by "All new mechanics" and by an all-checked custom setup parses and names real keys.
    launcher::s_settings all = Settings_(launcher::PRESET_ALL_NEW);
    launcher::s_settings custom = Settings_(launcher::PRESET_CUSTOM);
    for (const launcher::s_mechanic_category& category : list.categories) {
        for (const launcher::s_mechanic_ref& ref : category.settings) {
            launcher::Set_Mechanic(&custom, ref.name_space, ref.name, true);
        }
    }
    all.language = custom.language = launcher::LANGUAGE_RUSSIAN;
    for (const launcher::s_settings* settings : {&all, &custom}) {
        const config::s_cfg_document document = Parse_(launcher::Build_User_Cfg(*settings, &list));
        for (const config::s_cfg_statement& statement : document.statements) {
            if (statement.kind != config::CFG_STATEMENT_ASSIGNMENT || statement.path.size() < 2) continue;
            DOCTEST_INFO("key " << statement.path[0].raw << " " << statement.path[1].raw);
            DOCTEST_CHECK(!Schema_Block_(schema, statement.path[1].raw).empty());
        }
    }
}

DOCTEST_TEST_CASE("the settings catalog parses") {
    const char* json = R"([
  {"namespace": "diplomacy", "name": "vassals", "group": "gameplay", "category": "ai_and_diplomacy",
   "title": "Vassals", "description": "A weak empire \"bows\".\nSecond line \u00e9.", "remarks": "False preserves classic behavior.",
   "type": "bool", "default": false},
  {"namespace": "interface", "name": "language", "group": "interface", "category": "interface_and_presentation",
   "title": "Interface language", "description": "d", "remarks": "", "type": "enum", "default": "en",
   "values": [{"name": "en", "title": ""}, {"name": "ru", "title": "Russian"}]},
  {"namespace": "colony", "name": "limit", "group": "gameplay", "category": "colonies_and_economy",
   "title": "Limit", "description": "", "remarks": "", "type": "int", "default": 7, "min": 1, "max": 9}
])";
    launcher::s_catalog catalog;
    DOCTEST_REQUIRE(launcher::Parse_Catalog(json, &catalog));
    DOCTEST_REQUIRE(catalog.entries.size() == 3);
    const launcher::s_catalog_entry* vassals = catalog.Find("diplomacy", "vassals");
    DOCTEST_REQUIRE(vassals != nullptr);
    DOCTEST_CHECK(vassals->title == "Vassals");
    DOCTEST_CHECK(vassals->description == "A weak empire \"bows\".\nSecond line \xC3\xA9.");
    DOCTEST_CHECK(vassals->type == "bool");
    DOCTEST_CHECK(!vassals->default_bool);
    DOCTEST_CHECK(vassals->category == "ai_and_diplomacy");
    DOCTEST_CHECK(catalog.Find("interface", "language")->default_text == "en");
    DOCTEST_CHECK(catalog.Find("colony", "limit")->default_text == "7");
    DOCTEST_CHECK(catalog.Find("colony", "nothing") == nullptr);

    launcher::s_catalog bad;
    DOCTEST_CHECK(!launcher::Parse_Catalog("[{\"name\": ", &bad));
    DOCTEST_CHECK(!launcher::Parse_Catalog("{\"a\": 1}", &bad));
    DOCTEST_CHECK(bad.entries.empty());
    DOCTEST_CHECK(launcher::Parse_Catalog("[]", &bad));
}

DOCTEST_TEST_CASE("mechanics boxes go to user.cfg and come back") {
    launcher::s_mechanics_list list;
    DOCTEST_REQUIRE(launcher::Parse_Mechanics_List(
        "// a comment\n{ categories: [ { id: \"a\", title: \"A\", settings: [\n"
        "  { namespace: \"diplomacy\", name: \"vassals\" }, { namespace: \"spy\", name: \"hired_pirates\" },\n"
        "], }, ], }\n", &list));
    DOCTEST_REQUIRE(list.categories.size() == 1);
    DOCTEST_CHECK(list.Count() == 2);

    launcher::s_settings settings = Settings_(launcher::PRESET_CUSTOM);
    launcher::Set_Mechanic(&settings, "diplomacy", "vassals", true);
    launcher::Set_Mechanic(&settings, "spy", "not_in_the_list", true);
    DOCTEST_CHECK(launcher::Mechanic_On(settings, "diplomacy", "vassals"));
    DOCTEST_CHECK(!launcher::Mechanic_On(settings, "spy", "hired_pirates"));
    const std::string text = launcher::Build_User_Cfg(settings, &list);
    DOCTEST_CHECK(Has_(text, "\ndiplomacy vassals = true;\n"));
    DOCTEST_CHECK(!Has_(text, "hired_pirates"));
    DOCTEST_CHECK(!Has_(text, "not_in_the_list"));

    launcher::s_settings read;
    DOCTEST_REQUIRE(launcher::Read_User_Cfg(text, &read));
    DOCTEST_CHECK(launcher::Mechanic_On(read, "diplomacy", "vassals"));
    DOCTEST_CHECK(!launcher::Mechanic_On(read, "spy", "hired_pirates"));

    launcher::Set_Mechanic(&settings, "diplomacy", "vassals", false);
    launcher::Set_Mechanic(&settings, "spy", "hired_pirates", true);
    const std::string again = launcher::Build_User_Cfg(settings, &list);
    DOCTEST_CHECK(!Has_(again, "vassals"));
    DOCTEST_CHECK(Has_(again, "\nspy hired_pirates = true;\n"));

    // other presets write no boxes; the launcher.ini keeps them
    DOCTEST_CHECK(!Has_(launcher::Build_User_Cfg(Settings_(launcher::PRESET_CLASSIC), &list), "vassals"));
    launcher::s_settings ini_read;
    launcher::Read_Launcher_Ini(launcher::Build_Launcher_Ini(settings), &ini_read);
    DOCTEST_CHECK(launcher::Mechanic_On(ini_read, "spy", "hired_pirates"));

    launcher::s_mechanics_list broken;
    DOCTEST_CHECK(!launcher::Parse_Mechanics_List("{ categories: [ { id: \"a\", settings: [ { namespace: \"X y\", name: \"z\" } ] } ] }", &broken));
}

DOCTEST_TEST_CASE("the preset All new mechanics is Living rivals plus every box but the bench tools") {
    launcher::s_mechanics_list list;
    DOCTEST_REQUIRE(launcher::Parse_Mechanics_List(Mechanics_Text_(), &list));
    const std::string all = launcher::Build_User_Cfg(Settings_(launcher::PRESET_ALL_NEW), &list);
    DOCTEST_CHECK(Has_(all, "# preset: all_new"));
    DOCTEST_CHECK(Has_(all, "advisor rules_phases = true;"));
    DOCTEST_CHECK(Has_(all, "colony misc_costs spy = 60;"));
    DOCTEST_CHECK(Has_(all, "\ndiplomacy memory = true;\n"));
    DOCTEST_CHECK(Has_(all, "\nspy operations = 16383;\n"));
    DOCTEST_CHECK(Has_(all, "\nspy network_size_step = 8;\n"));
    DOCTEST_CHECK(Has_(all, "\ndiplomacy memory_weary_limit = 30;\n"));
    DOCTEST_CHECK(Has_(all, "\nadvisor journal = full;\n"));
    DOCTEST_CHECK(Has_(all, "\ninterface mentat = summary;\n"));
    DOCTEST_CHECK(!Has_(launcher::Build_User_Cfg(Settings_(launcher::PRESET_LIVING_RIVALS), &list), "interface mentat"));
    for (const launcher::s_mechanic_category& category : list.categories) {
        for (const launcher::s_mechanic_ref& ref : category.settings) {
            DOCTEST_INFO("box " << ref.name_space << " " << ref.name);
            const std::string key = ref.name_space + "." + ref.name;
            const bool skipped = key == "gameplay.council_without_humans" || key == "dice.combat_log";
            DOCTEST_CHECK(Has_(all, ("\n" + ref.name_space + " " + ref.name + " = true;\n").c_str()) == !skipped);
        }
    }
    launcher::s_settings read;
    DOCTEST_REQUIRE(launcher::Read_User_Cfg(all, &read));
    DOCTEST_CHECK(read.preset == launcher::PRESET_ALL_NEW);
    // without the list the preset is Living rivals
    DOCTEST_CHECK(!Has_(launcher::Build_User_Cfg(Settings_(launcher::PRESET_ALL_NEW)), "vassals"));
}

DOCTEST_TEST_CASE("the text language is written to user.cfg and read back") {
    launcher::s_settings english = Settings_(launcher::PRESET_LIVING_RIVALS);
    const std::string en = launcher::Build_User_Cfg(english);
    DOCTEST_CHECK(!Has_(en, "interface language"));

    launcher::s_settings russian = Settings_(launcher::PRESET_CUSTOM);
    russian.language = launcher::LANGUAGE_RUSSIAN;
    const std::string ru = launcher::Build_User_Cfg(russian);
    DOCTEST_CHECK(Has_(ru, "\ninterface language = ru;\n"));
    DOCTEST_CHECK(Schema_Block_(Schema_(), "language").size() > 0);

    launcher::s_settings read;
    read.language = launcher::LANGUAGE_RUSSIAN;
    DOCTEST_REQUIRE(launcher::Read_User_Cfg(en, &read));
    DOCTEST_CHECK(read.language == launcher::LANGUAGE_ENGLISH);
    DOCTEST_REQUIRE(launcher::Read_User_Cfg(ru, &read));
    DOCTEST_CHECK(read.language == launcher::LANGUAGE_RUSSIAN);
    DOCTEST_CHECK(Has_(Schema_Block_(Schema_(), "language"), "namespace: \"interface\""));
}

DOCTEST_TEST_CASE("presets choose the mechanics") {
    const std::string classic = launcher::Build_User_Cfg(Settings_(launcher::PRESET_CLASSIC));
    DOCTEST_CHECK(Has_(classic, "advisor enabled = false;"));
    DOCTEST_CHECK(!Has_(classic, "enable "));
    DOCTEST_CHECK(!Has_(classic, "research_uses_own_output"));
    DOCTEST_CHECK(!Has_(classic, "diplomacy memory"));

    const std::string v150 = launcher::Build_User_Cfg(Settings_(launcher::PRESET_V150));
    DOCTEST_CHECK(Has_(v150, "\nenable v150;\n"));
    DOCTEST_CHECK(Has_(v150, "advisor enabled = false;"));

    const std::string rivals = launcher::Build_User_Cfg(Settings_(launcher::PRESET_LIVING_RIVALS));
    DOCTEST_CHECK(Has_(rivals, "advisor enabled = true;"));
    DOCTEST_CHECK(Has_(rivals, "advisor rules_outposts = false;"));
    DOCTEST_CHECK(Has_(rivals, "advisor rules_doctrine = true;"));
    DOCTEST_CHECK(Has_(rivals, "colony misc_costs spy = 60;"));
    DOCTEST_CHECK(Has_(rivals, "ai research_uses_own_output = true;"));
    DOCTEST_CHECK(Has_(rivals, "\ndiplomacy memory = true;\n"));
    DOCTEST_CHECK(Has_(rivals, "\nadvisor rules_phases = true;\n"));
}

DOCTEST_TEST_CASE("custom settings and mods round-trip through user.cfg") {
    launcher::s_settings settings = Settings_(launcher::PRESET_CUSTOM);
    settings.mechanics.advisor = true;
    settings.mechanics.espionage = 0;
    settings.mechanics.expansion = 2;
    settings.mechanics.rivals_adapt = false;
    settings.mechanics.diplomacy = true;
    settings.mechanics.research_fix = false;
    settings.mechanics.memory = false;
    settings.mechanics.phases = false;
    settings.mods = {"v150", "bad mod;", "extra_rules"};
    const std::string text = launcher::Build_User_Cfg(settings);
    DOCTEST_CHECK(!Has_(text, "diplomacy memory"));
    DOCTEST_CHECK(Has_(text, "\nenable v150;\n"));
    DOCTEST_CHECK(Has_(text, "\nenable extra_rules;\n"));
    DOCTEST_CHECK(!Has_(text, "bad mod"));
    DOCTEST_CHECK(Has_(text, "advisor rules_spies = false;"));
    DOCTEST_CHECK(Has_(text, "advisor rules_outposts = true;"));
    DOCTEST_CHECK(Has_(text, "advisor rules_lanchester = false;"));
    DOCTEST_CHECK(Has_(text, "advisor rules_phases = false;"));
    DOCTEST_CHECK(!Has_(text, "misc_costs"));

    launcher::s_settings read;
    DOCTEST_REQUIRE(launcher::Read_User_Cfg(text, &read));
    DOCTEST_CHECK(read.preset == launcher::PRESET_CUSTOM);
    DOCTEST_CHECK(read.mechanics.espionage == 0);
    DOCTEST_CHECK(read.mechanics.expansion == 2);
    DOCTEST_CHECK(!read.mechanics.rivals_adapt);
    DOCTEST_CHECK(read.mechanics.diplomacy);
    DOCTEST_CHECK(!read.mechanics.research_fix);
    DOCTEST_CHECK(!read.mechanics.memory);
    DOCTEST_CHECK(!read.mechanics.phases);
    DOCTEST_REQUIRE(read.mods.size() == 2);
    DOCTEST_CHECK(read.mods[0] == "v150");
    DOCTEST_CHECK(read.mods[1] == "extra_rules");
}

DOCTEST_TEST_CASE("a hand-written user.cfg is not read as the launcher's") {
    launcher::s_settings settings = Settings_(launcher::PRESET_CLASSIC);
    DOCTEST_CHECK(!launcher::Is_Launcher_User_Cfg("advisor enabled = true;\n"));
    DOCTEST_CHECK(!launcher::Read_User_Cfg("advisor enabled = true;\n", &settings));
    DOCTEST_CHECK(settings.preset == launcher::PRESET_CLASSIC);
}

DOCTEST_TEST_CASE("launcher.ini keeps the language, folders and preset") {
    launcher::s_settings settings = Settings_(launcher::PRESET_V150);
    settings.language = launcher::LANGUAGE_RUSSIAN;
    settings.game_dir[launcher::LANGUAGE_ENGLISH] = "E:\\Games\\MOO2\\v1_31eng";
    settings.game_dir[launcher::LANGUAGE_RUSSIAN] = "E:\\Games\\MOO2\\v1_31rus";
    settings.mechanics.espionage = 2;
    const std::string ini = launcher::Build_Launcher_Ini(settings);

    launcher::s_settings read;
    launcher::Read_Launcher_Ini(ini, &read);
    DOCTEST_CHECK(read.language == launcher::LANGUAGE_RUSSIAN);
    DOCTEST_CHECK(read.preset == launcher::PRESET_V150);
    DOCTEST_CHECK(read.game_dir[launcher::LANGUAGE_RUSSIAN] == "E:\\Games\\MOO2\\v1_31rus");
    DOCTEST_CHECK(read.mechanics.espionage == 2);

    const std::vector<std::string> args = launcher::Game_Arguments("orion2re.exe", read);
    DOCTEST_REQUIRE(args.size() == 3);
    DOCTEST_CHECK(args[2] == "E:\\Games\\MOO2\\v1_31rus");
    DOCTEST_CHECK(launcher::Language_For_Game_Dir("E:\\Games\\MOO2\\v1_31RUS") == launcher::LANGUAGE_RUSSIAN);
    DOCTEST_CHECK(launcher::Language_For_Game_Dir("E:\\Games\\MOO2\\v1_31eng") == launcher::LANGUAGE_ENGLISH);

    read.game_dir[launcher::LANGUAGE_RUSSIAN].clear();
    DOCTEST_CHECK(launcher::Game_Arguments("orion2re.exe", read).size() == 1);
}

} // DOCTEST_TEST_SUITE
