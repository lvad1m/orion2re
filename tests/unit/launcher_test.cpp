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
    settings.mods = {"v150", "bad mod;", "extra_rules"};
    const std::string text = launcher::Build_User_Cfg(settings);
    DOCTEST_CHECK(!Has_(text, "diplomacy memory"));
    DOCTEST_CHECK(Has_(text, "\nenable v150;\n"));
    DOCTEST_CHECK(Has_(text, "\nenable extra_rules;\n"));
    DOCTEST_CHECK(!Has_(text, "bad mod"));
    DOCTEST_CHECK(Has_(text, "advisor rules_spies = false;"));
    DOCTEST_CHECK(Has_(text, "advisor rules_outposts = true;"));
    DOCTEST_CHECK(Has_(text, "advisor rules_lanchester = false;"));
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
