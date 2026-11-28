#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "yyjson.h"

#include <map>
#include <set>
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
            names.insert(text.substr(i + 1, close - i - 1));
        }
        return names;
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
            result += long_names.count(text.substr(i + 1, close - i - 1)) ? std::string(14, 'W') : std::string(5, '9');
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
