// Numbers and dates in the player's language.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>

#include <catch2/catch_test_macros.hpp>

#include <ctime>
#include <fstream>
#include <string>

using namespace Arcbit;

namespace
{
// The engine's shipped languages (their loc.* settings), English to start.
struct Shipped
{
    Shipped()
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
    }
    ~Shipped() { Loc::Clear(); }
};

// 2026-10-18 17:42:05 in the machine's own time zone.
i64 Evening()
{
    std::tm t{};
    t.tm_year  = 2026 - 1900;
    t.tm_mon   = 9;
    t.tm_mday  = 18;
    t.tm_hour  = 17;
    t.tm_min   = 42;
    t.tm_sec   = 5;
    t.tm_isdst = -1;
    return static_cast<i64>(std::mktime(&t));
}

const std::string NarrowSpace = "\xE2\x80\xAF"; // French groups with U+202F
} // namespace

TEST_CASE("Numbers take each language's separators", "[core][loc]")
{
    Shipped shipped;
    CHECK(Loc::FormatNumber(1234567.5) == "1,234,567.5");
    CHECK(Loc::FormatNumber(-1234) == "-1,234");
    CHECK(Loc::FormatNumber(999) == "999");
    CHECK(Loc::FormatNumber(3.14159, 2) == "3.14");
    CHECK(Loc::FormatNumber(5, 1) == "5.0");

    REQUIRE(Loc::SetLanguage("fr"));
    CHECK(Loc::FormatNumber(1234567.5) == "1" + NarrowSpace + "234" + NarrowSpace + "567,5");
    CHECK(Loc::FormatNumber(0.25) == "0,25");
}

TEST_CASE("Placeholders format numbers unless told otherwise", "[core][loc]")
{
    Shipped         shipped;
    const LocArg    big[]   = { { "n", 12345 } };
    const LocArg    small[] = { { "n", 0.33f } };
    CHECK(Loc::Interpolate("{n}", big) == "12,345");
    CHECK(Loc::Interpolate("{n:raw}", big) == "12345");
    CHECK(Loc::Interpolate("{n:n2}", big) == "12,345.00");
    CHECK(Loc::Interpolate("{n}", small) == "0.33"); // the float as written, not its double (0.330000013...)
    const LocArg text[] = { { "n", "1234" } };
    CHECK(Loc::Interpolate("{n:n2}", text) == "1234"); // text is never reformatted
}

TEST_CASE("Dates follow each language's patterns and month names", "[core][loc]")
{
    Shipped   shipped;
    const i64 when = Evening();
    CHECK(Loc::FormatDate(when) == "Oct 18, 2026");
    CHECK(Loc::FormatDate(when, "time") == "5:42 PM");
    CHECK(Loc::FormatDate(when, "datetime") == "Oct 18, 2026, 5:42 PM");
    const LocArg saved[] = { { "saved", when } };
    CHECK(Loc::Interpolate("{saved:date}", saved) == "Oct 18, 2026");

    REQUIRE(Loc::SetLanguage("fr"));
    CHECK(Loc::FormatDate(when, "datetime") == "18 oct. 2026, 17:42");
    REQUIRE(Loc::SetLanguage("ja"));
    CHECK(Loc::FormatDate(when, "datetime") == "2026\xE5\xB9\xB4" "10\xE6\x9C\x88" "18\xE6\x97\xA5 17:42"); // 2026nen 10gatsu 18nichi
}

TEST_CASE("Without a locale, numbers and dates fall back to plain forms", "[core][loc]")
{
    Loc::Clear();
    CHECK(Loc::FormatNumber(1234.5) == "1,234.5");
    CHECK(Loc::FormatDate(Evening()) == "2026-10-18");
    CHECK(Loc::FormatDate(Evening(), "time") == "17:42");
}

TEST_CASE("The pseudo-locale keeps the format settings", "[core][loc]")
{
    Shipped shipped;
    Loc::AddPseudoLocale();
    REQUIRE(Loc::SetLanguage(Loc::PseudoCode));
    CHECK(Loc::FormatNumber(1234.5) == "1,234.5");
}
