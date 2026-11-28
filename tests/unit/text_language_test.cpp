#include "pch.h"

#include "doctest/doctest.h"

#include "game/text_language.h"

DOCTEST_TEST_CASE("the twin table maps both ways, case-insensitively, and unknown names pass through") {
    DOCTEST_CHECK(std::string(text_language::Twin_Name("estrings.lbx")) == "estrruss.lbx");
    DOCTEST_CHECK(std::string(text_language::Twin_Name("ESTRINGS.LBX")) == "estrruss.lbx");
    DOCTEST_CHECK(std::string(text_language::Twin_Name("Fonts.lbx")) == "fntsr.lbx");
    DOCTEST_CHECK(std::string(text_language::Twin_Name("rstring0.lbx")) == "rstrrus0.lbx");
    DOCTEST_CHECK(std::string(text_language::Twin_Name("hestrngs.lbx")) == "hrstrngs.lbx");
    DOCTEST_CHECK(std::string(text_language::Original_Name("JIMTXTR.LBX")) == "jimtext.lbx");
    DOCTEST_CHECK(std::string(text_language::Original_Name("crdtsru.lbx")) == "credits.lbx");
    DOCTEST_CHECK(text_language::Twin_Name("combat.lbx") == nullptr);
    DOCTEST_CHECK(text_language::Twin_Name("estrruss.lbx") == nullptr);
    DOCTEST_CHECK(text_language::Original_Name("combat.lbx") == nullptr);
    DOCTEST_CHECK(text_language::Twin_Name(nullptr) == nullptr);

    // Every table entry round-trips.
    static const char* const originals[] = {"antarmsg", "billtex2", "billtext", "councmsg", "credits", "diplomse",
        "estrings", "eventmsg", "fonts", "herodata", "help", "hestrngs", "jimtext", "jimtext2", "kentext", "kentext1",
        "maintext", "racename", "racestuf", "rstring0", "shipname", "skildesc", "starname", "techdesc", "techname"};
    for (const char* name : originals) {
        const std::string original = std::string(name) + ".lbx";
        const char* twin = text_language::Twin_Name(original.c_str());
        DOCTEST_REQUIRE(twin != nullptr);
        DOCTEST_CHECK(original == text_language::Original_Name(twin));
    }
}

DOCTEST_TEST_CASE("From_Utf8 writes Cyrillic as CP866 and everything unknown as a question mark") {
    std::string bytes;
    DOCTEST_CHECK(text_language::From_Utf8("Hello, world 123", &bytes));
    DOCTEST_CHECK(bytes == "Hello, world 123");

    DOCTEST_CHECK(text_language::From_Utf8("\xD0\x90\xD0\xAF", &bytes)); // АЯ
    DOCTEST_CHECK(bytes == "\x80\x9F");
    DOCTEST_CHECK(text_language::From_Utf8("\xD0\xB0\xD0\xBF\xD1\x80\xD1\x8F", &bytes)); // апря
    DOCTEST_CHECK(bytes == "\xA0\xAF\xE0\xEF");
    DOCTEST_CHECK(text_language::From_Utf8("\xD0\x81\xD1\x91", &bytes)); // Её
    DOCTEST_CHECK(bytes == "\xF0\xF1");
    DOCTEST_CHECK(text_language::From_Utf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 1", &bytes)); // Привет 1
    DOCTEST_CHECK(bytes == "\x8F\xE0\xA8\xA2\xA5\xE2 1");

    // Pseudographics: box drawing and block elements.
    DOCTEST_CHECK(text_language::From_Utf8("\xE2\x94\x80\xE2\x95\x94\xE2\x96\x88", &bytes)); // ─ ╔ █
    DOCTEST_CHECK(bytes == "\xC4\xC9\xDB");

    // Unknown characters (an accent, a Chinese character, a bare continuation byte).
    DOCTEST_CHECK_FALSE(text_language::From_Utf8("a\xC3\xA9" "b", &bytes));
    DOCTEST_CHECK(bytes == "a?b");
    DOCTEST_CHECK_FALSE(text_language::From_Utf8("\xE4\xB8\xAD", &bytes));
    DOCTEST_CHECK(bytes == "?");
    DOCTEST_CHECK_FALSE(text_language::From_Utf8("x\x80y", &bytes));
    DOCTEST_CHECK(bytes == "x?y");
}

DOCTEST_TEST_CASE("Cp866_From_Codepoint covers the full Russian alphabet") {
    uint8_t byte = 0;
    int mapped = 0;
    for (uint32_t cp = 0x0410; cp <= 0x044F; ++cp) {
        DOCTEST_CHECK(text_language::Cp866_From_Codepoint(cp, &byte));
        ++mapped;
    }
    DOCTEST_CHECK(mapped == 64);
    DOCTEST_CHECK_FALSE(text_language::Cp866_From_Codepoint(0x0402, &byte));
    DOCTEST_CHECK_FALSE(text_language::Cp866_From_Codepoint(0x00E9, &byte));
}
