#include "TestSupport.h"

#include <arcbit/core/Loc.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
// Loc is a global table; start and end every test empty.
struct LocScope
{
    LocScope() { Loc::Clear(); }
    ~LocScope() { Loc::Clear(); }
};
} // namespace

TEST_CASE("Loc resolves keys and falls back to the key itself", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("en.json", R"({ "ui.ok": "OK", "ui.count": 3 })")));

    CHECK(Loc::Get("ui.ok") == "OK");
    CHECK(Loc::Get("ui.missing") == "ui.missing");
    CHECK(Loc::Get("ui.count") == "ui.count"); // non-string values are ignored
    CHECK(&Loc::Get("ui.missing") == &Loc::Get("ui.missing")); // stable reference for missing keys
}

TEST_CASE("Later locale files override earlier ones; Clear empties the table", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("base.json", R"({ "a": "base-a", "b": "base-b" })")));
    REQUIRE(Loc::Load(dir.Write("patch.json", R"({ "b": "patched-b" })")));
    CHECK(Loc::Get("a") == "base-a");
    CHECK(Loc::Get("b") == "patched-b");

    Loc::Clear();
    CHECK(Loc::Get("a") == "a");
}

TEST_CASE("Unreadable locale files fail without touching the table", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("good.json", R"({ "k": "v" })")));
    CHECK_FALSE(Loc::Load(dir.File("missing.json").string()));
    CHECK_FALSE(Loc::Load(dir.Write("bad.json", R"({ "k": )")));
    CHECK(Loc::Get("k") == "v");
}

TEST_CASE("The shipped English locale loads and covers the engine UI", "[core][loc][shipped]")
{
    LocScope scope;
    REQUIRE(Loc::Load(ArcbitTests::SourcePath("engine/assets/locale/en.json").string()));
    CHECK(Loc::Get("ui.mainmenu.title") != "ui.mainmenu.title");
}
