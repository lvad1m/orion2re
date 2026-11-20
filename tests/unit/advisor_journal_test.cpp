#include "pch.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "doctest/doctest.h"
#include "game/advisor.h"
#include "game/advisor_journal.h"

namespace {
    std::string Escaped(const std::string& text) {
        std::string out;
        advisor::Append_Json_Escaped(text, &out);
        return out;
    }

    std::vector<std::string> Read_Lines(const std::filesystem::path& path) {
        std::vector<std::string> lines;
        std::ifstream file(path, std::ios::binary);
        std::string line;
        while (std::getline(file, line)) lines.push_back(line);
        return lines;
    }

    std::vector<std::filesystem::path> Jsonl_Files(const std::filesystem::path& directory) {
        std::vector<std::filesystem::path> files;
        for (const auto& item : std::filesystem::directory_iterator(directory))
            if (item.path().extension() == ".jsonl") files.push_back(item.path());
        return files;
    }

    struct s_journal_directory_scope {
        std::filesystem::path path = std::filesystem::temp_directory_path() / "orion2re_advisor_journal_test";

        s_journal_directory_scope() {
            advisor::Close_Journal();
            std::error_code error;
            std::filesystem::remove_all(path, error);
            advisor::Set_Journal_Directory(path.generic_string() + "/");
        }

        ~s_journal_directory_scope() {
            advisor::Close_Journal();
            advisor::Set_Journal_Directory("");
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    advisor::s_directive Known_Directive() {
        advisor::s_directive d;
        d.source = advisor::SOURCE_RULES;
        d.issued_stardate = 100;
        d.expires_stardate = 110;
        d.objective = 2;
        d.posture = advisor::POSTURE_ATTACK_CURRENT_ENEMIES;
        d.attitude[1] = advisor::ATTITUDE_WAR;
        d.attitude[2] = advisor::ATTITUDE_ALLY;
        d.main_rival = 1;
        d.weights[advisor::WEIGHT_FLEET] = advisor::LEVEL_HIGH;
        d.weights[advisor::WEIGHT_RESEARCH] = advisor::LEVEL_LOW;
        d.fleet_doctrine = advisor::DOCTRINE_ANTI_MISSILE;
        d.spy_mission = advisor::SPY_STEAL_TECH;
        d.spy_target = 1;
        d.rule_id = 7;
        return d;
    }
} // namespace

DOCTEST_TEST_CASE("journal escapes JSON strings") {
    DOCTEST_CHECK(Escaped("a\"b") == "a\\\"b");
    DOCTEST_CHECK(Escaped("a\\b") == "a\\\\b");
    DOCTEST_CHECK(Escaped("a\nb\r\t") == "a\\nb\\r\\t");
    DOCTEST_CHECK(Escaped(std::string("a\x01"
                                      "b\x1f")) == "a\\u0001b\\u001f");
    DOCTEST_CHECK(Escaped("\b\f") == "\\b\\f");
    const std::string cyrillic = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    DOCTEST_CHECK(Escaped(cyrillic) == cyrillic);
}

DOCTEST_TEST_CASE("journal DECISIONS line has fixed keys and snake_case names") {
    const advisor::s_directive d = Known_Directive();
    advisor::s_journal_entry entry;
    entry.stardate = 100;
    entry.player = 3;
    entry.directive = &d;
    entry.accepted = true;
    entry.rule_note = "rule \"7\"";
    std::string line;
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_DECISIONS, &line);
    const std::string expected =
        "{\"stardate\":100,\"player\":3,\"source\":\"rules\",\"accepted\":true,\"rejection\":\"\","
        "\"rule_note\":\"rule \\\"7\\\"\",\"directive\":{\"objective\":2,"
        "\"posture\":\"attack_current_enemies\","
        "\"attitude\":[\"keep\",\"war\",\"ally\",\"keep\",\"keep\",\"keep\",\"keep\",\"keep\"],"
        "\"main_rival\":1,\"weights\":{\"fleet\":\"high\",\"defense\":\"keep\",\"colonization\":\"keep\","
        "\"economy\":\"keep\",\"food\":\"keep\",\"research\":\"low\"},"
        "\"research_focus\":\"keep\",\"fleet_doctrine\":\"anti_missile\",\"spy_target\":1,"
        "\"spy_mission\":\"steal_tech\",\"issued\":100,\"expires\":110,\"rule_id\":7}}";
    DOCTEST_CHECK(line == expected);

    std::string again;
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_DECISIONS, &again);
    DOCTEST_CHECK(again == line);
}

DOCTEST_TEST_CASE("journal line for a missing directive and for KEEP players") {
    advisor::s_journal_entry entry;
    entry.stardate = 5;
    entry.player = 1;
    entry.rejection = "bad";
    std::string line;
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_DECISIONS, &line);
    DOCTEST_CHECK(line ==
        "{\"stardate\":5,\"player\":1,\"source\":\"none\",\"accepted\":false,\"rejection\":\"bad\","
        "\"rule_note\":\"\",\"directive\":null}");

    const advisor::s_directive d;
    entry.directive = &d;
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_DECISIONS, &line);
    DOCTEST_CHECK(line.find("\"objective\":null") != std::string::npos);
    DOCTEST_CHECK(line.find("\"main_rival\":null") != std::string::npos);
    DOCTEST_CHECK(line.find("\"spy_target\":null") != std::string::npos);
}

DOCTEST_TEST_CASE("journal FULL line adds the summary, DECISIONS does not") {
    advisor::s_empire_view view;
    view.self.player = 2;
    view.self.stardate = 100;
    advisor::s_journal_entry entry;
    entry.stardate = 100;
    entry.player = 2;
    entry.view = &view;
    std::string full, decisions, summary;
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_FULL, &full);
    advisor::Format_Journal_Line(&entry, advisor::JOURNAL_DECISIONS, &decisions);
    advisor::Format_Empire_Summary(&view, &summary);
    DOCTEST_CHECK(decisions.find("\"summary\"") == std::string::npos);
    DOCTEST_CHECK(full.find(",\"summary\":\"") != std::string::npos);
    std::string escaped;
    advisor::Append_Json_Escaped(summary, &escaped);
    DOCTEST_CHECK(full.find("\"summary\":\"" + escaped + "\"}") != std::string::npos);
    DOCTEST_CHECK(full.find('\n') == std::string::npos);
}

DOCTEST_TEST_CASE("journal writes lines to a session file and Close starts a new one") {
    s_journal_directory_scope scope;
    advisor::Write_Journal_Line("{\"a\":1}");
    advisor::Write_Journal_Line("{\"a\":2}");
    advisor::Close_Journal();
    std::vector<std::filesystem::path> files = Jsonl_Files(scope.path);
    DOCTEST_REQUIRE(files.size() == 1);
    const std::vector<std::string> lines = Read_Lines(files[0]);
    DOCTEST_REQUIRE(lines.size() == 2);
    DOCTEST_CHECK(lines[0] == "{\"a\":1}");
    DOCTEST_CHECK(lines[1] == "{\"a\":2}");

    advisor::Write_Journal_Line("{\"a\":3}");
    advisor::Close_Journal();
    files = Jsonl_Files(scope.path);
    DOCTEST_CHECK(files.size() == 2);
}
