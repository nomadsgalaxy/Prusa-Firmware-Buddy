#include "catch2/catch.hpp"

#include "provider.h"
#include "translation_provider_FILE.hpp"
#include <format>

static void test_language(const std::string &language_code) {
    INFO("Testing language: " << language_code);

    const auto mo_path = std::format("MO/{}.mo", language_code);
    const auto txt_path = std::format("{}.txt", language_code);

    // initialize translation provider
    FILETranslationProvider provider { mo_path.c_str() };
    REQUIRE(provider.EnsureFile());

    // load reference strings
    std::deque<std::string> original;
    std::deque<std::string> translated;
    REQUIRE(LoadTranslatedStringsFile("keys.txt", &original));
    REQUIRE(LoadTranslatedStringsFile(txt_path.c_str(), &translated));

    // need to have at least the same amount of translations as the keys (normally there will be an exact number of them)
    REQUIRE(original.size() <= translated.size());

    // do the checking
    REQUIRE(CheckAllTheStrings(original, translated, provider, language_code.c_str()));
}

TEST_CASE("providerFILE::Translations test", "[translator]") {
    test_language("cs");
    test_language("de");
    test_language("es");
    test_language("fr");
    test_language("it");
    test_language("pl");
    test_language("ja");
    test_language("uk");
}

TEST_CASE("providerFILE::bad files test", "[translator]") {
    FILETranslationProvider nonExistingFile("nOnExIsTiNg.mo");
    FILETranslationProvider shortFile("MO/short.mo");
    FILETranslationProvider badMagic("MO/magic.mo");
    FILETranslationProvider bigEnd("MO/bigEnd.mo");

    REQUIRE(!nonExistingFile.EnsureFile());
    REQUIRE(shortFile.EnsureFile());
    REQUIRE(!badMagic.EnsureFile());
    REQUIRE(!bigEnd.EnsureFile());
    static const char *key = "Language";
    // the file is short and should return key string
    REQUIRE(CompareStringViews(shortFile.GetText(key), string_view_utf8::MakeRAM(key), "ts"));
}
