#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "yyjson.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <string>

namespace {
    struct s_texts_scope {
        test::s_saved_value<uint8_t> language{text_language::language};
        s_texts_scope() { texts::Reset(); }
        ~s_texts_scope() { texts::Reset(); }
    };

    void Flatten_(yyjson_val* value, const std::string& prefix, std::map<std::string, std::string>* out) {
        if (yyjson_is_obj(value)) {
            size_t i, count;
            yyjson_val *key, *child;
            yyjson_obj_foreach(value, i, count, key, child) {
                Flatten_(child, prefix.empty() ? std::string(yyjson_get_str(key)) : prefix + "." + yyjson_get_str(key), out);
            }
        } else if (yyjson_is_str(value)) {
            (*out)[prefix] = std::string(yyjson_get_str(value), yyjson_get_len(value));
        } else {
            DOCTEST_FAIL("a text value that is not a string: " << prefix);
        }
    }

    std::map<std::string, std::string> Read_Flat_(const char* name) {
        const std::string source = test::Read_Base_Text_File(name);
        DOCTEST_REQUIRE_MESSAGE(!source.empty(), test::Base_Texts_Dir().string() << "/" << name);
        yyjson_read_err error{};
        yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(source.data()), source.size(), YYJSON_READ_JSON5, nullptr, &error);
        DOCTEST_REQUIRE_MESSAGE(doc != nullptr, name << ": " << error.msg << " at " << error.pos);
        std::map<std::string, std::string> flat;
        Flatten_(yyjson_doc_get_root(doc), std::string(), &flat);
        yyjson_doc_free(doc);
        return flat;
    }

    std::set<std::string> Placeholders_(const std::string& text) {
        std::set<std::string> names;
        for (size_t i = text.find('{'); i != std::string::npos; i = text.find('{', i + 1)) {
            const size_t close = text.find('}', i);
            if (close == std::string::npos) break;
            names.insert(text.substr(i + 1, close - i - 1).substr(0, text.substr(i + 1, close - i - 1).find('|')));
        }
        return names;
    }

    // The forms of every plural placeholder {name|a|b|...} in the text: the number of forms of each.
    std::vector<size_t> Plural_Form_Counts_(const std::string& text) {
        std::vector<size_t> counts;
        for (size_t i = text.find('{'); i != std::string::npos; i = text.find('{', i + 1)) {
            const size_t close = text.find('}', i);
            if (close == std::string::npos) break;
            const std::string body = text.substr(i + 1, close - i - 1);
            if (body.find('|') != std::string::npos) counts.push_back(static_cast<size_t>(std::count(body.begin(), body.end(), '|')));
        }
        return counts;
    }

    // The text with every placeholder replaced by its worst-case value: names 14 characters, numbers 5 digits.
    std::string Worst_Case_(const std::string& text) {
        static const std::set<std::string> long_names = {"place", "name", "race"};
        std::string result;
        for (size_t i = 0; i < text.size();) {
            const size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
            if (close == std::string::npos) {
                result += text[i++];
                continue;
            }
            const std::string body = text.substr(i + 1, close - i - 1);
            const size_t bar = body.find('|');
            if (bar != std::string::npos) {
                // a plural placeholder: five digits, a space and the longest form
                size_t longest = 0;
                for (size_t from = bar + 1; from <= body.size();) {
                    size_t next = body.find('|', from);
                    if (next == std::string::npos) next = body.size();
                    longest = std::max(longest, next - from);
                    from = next + 1;
                }
                result += std::string(5, '9') + ' ' + std::string(longest, 'W');
            } else {
                result += long_names.count(body) ? std::string(14, 'W') : std::string(5, '9');
            }
            i = close + 1;
        }
        return result;
    }
} // namespace

DOCTEST_TEST_SUITE("texts") {

DOCTEST_TEST_CASE("Get: the current language, else English, else the key")
{
    s_texts_scope scope;
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({a: {b: "Hello {x}"}, only_en: "English only"})"));
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_RU, R"({a: {b: "Привет {x}"}})"));
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_CHECK(std::string(texts::Get("a.b")) == "Hello {x}");
    DOCTEST_CHECK(std::string(texts::Get("only_en")) == "English only");
    text_language::language = text_language::LANGUAGE_RU;
    DOCTEST_CHECK(std::string(texts::Get("a.b")) == "\x8F\xE0\xA8\xA2\xA5\xE2 {x}");  // CP866
    DOCTEST_CHECK(std::string(texts::Get("only_en")) == "English only");  // a missing Russian key falls back to English
    DOCTEST_CHECK(std::string(texts::Get("no.such.key")) == "no.such.key");
    DOCTEST_CHECK(std::string(texts::Get("no.such.key")) == "no.such.key");
}

DOCTEST_TEST_CASE("Load_Source: a later text replaces the same keys; a broken text changes nothing")
{
    s_texts_scope scope;
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({a: "base", b: "kept"})"));
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({a: "mod"})"));
    DOCTEST_CHECK(std::string(texts::Get("a")) == "mod");
    DOCTEST_CHECK(std::string(texts::Get("b")) == "kept");
    DOCTEST_CHECK_FALSE(texts::Load_Source(text_language::LANGUAGE_EN, R"({a: "broken", )"));
    DOCTEST_CHECK_FALSE(texts::Load_Source(text_language::LANGUAGE_EN, R"(["not", "an object"])"));
    DOCTEST_CHECK(std::string(texts::Get("a")) == "mod");
    // A broken Russian file leaves English in force.
    DOCTEST_CHECK_FALSE(texts::Load_Source(text_language::LANGUAGE_RU, "{ nonsense"));
    text_language::language = text_language::LANGUAGE_RU;
    DOCTEST_CHECK(std::string(texts::Get("a")) == "mod");
}

DOCTEST_TEST_CASE("Format: named placeholders, reordered, unknown ones kept, no printf")
{
    s_texts_scope scope;
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({t: "{a} then {b} and {a} again {c} 100% %d %s {", u: "{b}-{a}", v: "x{e}y"})"));
    char out[128];
    texts::Format(out, sizeof(out), "t", {{"a", "1"}, {"b", "two"}});
    DOCTEST_CHECK(std::string(out) == "1 then two and 1 again {c} 100% %d %s {");  // unknown {c} stays as written
    texts::Format(out, sizeof(out), "u", {{"a", "1"}, {"b", "2"}, {"unused", "3"}});
    DOCTEST_CHECK(std::string(out) == "2-1");
    texts::Format(out, sizeof(out), "v", {{"e", ""}});
    DOCTEST_CHECK(std::string(out) == "xy");  // an empty value leaves the placeholder empty
    char small[6];
    texts::Format(small, sizeof(small), "t", {{"a", "1"}, {"b", "two"}});
    DOCTEST_CHECK(std::string(small) == "1 the");  // cut, terminated
    texts::Format(out, sizeof(out), "missing.key", {{"a", "1"}});
    DOCTEST_CHECK(std::string(out) == "missing.key");
}

DOCTEST_TEST_CASE("Format: the plural placeholder {name|form1|form2|form3}")
{
    s_texts_scope scope;
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({t: "in {n|turn|turns}", u: "{n|a|b|c}", f: "{n|only}", x: "{n|a|b} {n}", odd: "{n|a|b", odd2: "{ {n|a|b}", unk: "{k|a|b}"})"));
    char out[128];
    auto format = [&](const char* key, const char* value) {
        texts::Format(out, sizeof(out), key, {{"n", value}});
        return std::string(out);
    };
    text_language::language = text_language::LANGUAGE_RU;
    const struct { const char* n; const char* form; } ru[] = {{"0", "c"}, {"1", "a"}, {"2", "b"}, {"4", "b"}, {"5", "c"}, {"11", "c"}, {"12", "c"}, {"14", "c"},
        {"21", "a"}, {"22", "b"}, {"25", "c"}, {"101", "a"}, {"111", "c"}, {"-1", "a"}, {"-2", "b"}};
    for (const auto& row : ru) {
        DOCTEST_CAPTURE(row.n);
        DOCTEST_CHECK(format("u", row.n) == std::string(row.n) + " " + row.form);
    }
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_CHECK(format("t", "0") == "in 0 turns");
    DOCTEST_CHECK(format("t", "1") == "in 1 turn");
    DOCTEST_CHECK(format("t", "2") == "in 2 turns");
    DOCTEST_CHECK(format("t", "-1") == "in -1 turn");
    DOCTEST_CHECK(format("u", "5") == "5 b");  // English uses the first two forms
    DOCTEST_CHECK(format("u", "1") == "1 a");
    text_language::language = text_language::LANGUAGE_RU;
    DOCTEST_CHECK(format("f", "2") == "2 only");  // fewer forms: the last one given
    DOCTEST_CHECK(format("u", "many") == "many c");  // not an integer: the value and the last form
    DOCTEST_CHECK(format("u", "") == " c");
    DOCTEST_CHECK(format("x", "3") == "3 b 3");  // a plain {n} stays plain
    DOCTEST_CHECK(format("odd", "3") == "{n|a|b");  // unclosed: left as written
    DOCTEST_CHECK(format("odd2", "3") == "{ 3 b");  // no nesting: the inner placeholder works, the stray brace stays
    DOCTEST_CHECK(format("unk", "3") == "{k|a|b}");  // an unknown name stays as written
    // Expand and Format_Utf8 share the rule.
    char utf8[64];
    texts::Format_Utf8(utf8, sizeof(utf8), "u", {{"n", "3"}});
    DOCTEST_CHECK(std::string(utf8) == "3 b");
    DOCTEST_CHECK(texts::Expand("u", {{"n", "5"}}, false) == "5 c");
}

DOCTEST_TEST_CASE("the shipped text files: Russian keys and placeholders match English, CP866 clean, every choice text fits")
{
    s_texts_scope scope;
    const std::map<std::string, std::string> en = Read_Flat_("en.json5");
    const std::map<std::string, std::string> ru = Read_Flat_("ru.json5");
    DOCTEST_CHECK(en.size() > 20);
    for (const auto& [key, text] : ru) {
        DOCTEST_CAPTURE(key);
        const auto english = en.find(key);
        DOCTEST_REQUIRE(english != en.end());
        const std::set<std::string> allowed = Placeholders_(english->second);
        for (const std::string& name : Placeholders_(text)) {
            DOCTEST_CAPTURE(name);
            DOCTEST_CHECK(allowed.count(name) == 1);
        }
        std::string bytes;
        DOCTEST_CHECK(text_language::From_Utf8(text, &bytes));
        DOCTEST_CHECK(bytes.find('?') == std::string::npos);
        for (const size_t bars : Plural_Form_Counts_(text)) DOCTEST_CHECK(bars == 3);  // three forms in Russian
    }
    for (const auto& [key, text] : en) {
        DOCTEST_CAPTURE(key);
        for (const size_t bars : Plural_Form_Counts_(text)) DOCTEST_CHECK(bars == 2);  // two forms in English
    }
    // Every English key has a Russian one, so the Russian window is whole.
    for (const auto& [key, text] : en) {
        DOCTEST_CAPTURE(key);
        DOCTEST_CHECK(ru.count(key) == 1);
    }
    choices::s_text sizes;
    for (const std::map<std::string, std::string>* table : {&en, &ru}) {
        for (const auto& [key, text] : *table) {
            if (key.rfind("choices.", 0) != 0) continue;
            std::string bytes;
            text_language::From_Utf8(Worst_Case_(text), &bytes);
            size_t capacity = 0;
            if (key.size() >= 6 && key.compare(key.size() - 6, 6, ".title") == 0) capacity = sizeof(sizes.title);
            else if (key.size() >= 5 && key.compare(key.size() - 5, 5, ".body") == 0) capacity = sizeof(sizes.body);
            else if (key.find(".option") != std::string::npos) capacity = sizeof(sizes.option[0]);
            else continue;  // the colony fallback and other small pieces are checked through their users
            DOCTEST_CAPTURE(key);
            DOCTEST_CAPTURE(bytes.size());
            DOCTEST_CHECK(bytes.size() < capacity);
        }
    }
}

}
