#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/ui/CreditsScreen.h>
#include <arcbit/ui/PauseMenuScreen.h>
#include <arcbit/ui/UILoader.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>

using namespace Arcbit;
using nlohmann::json;

namespace
{
constexpr UIRect Screen{ 0.0f, 0.0f, 1920.0f, 1080.0f };

void CheckRect(const UIRect r, const f32 x, const f32 y, const f32 w, const f32 h)
{
    CHECK(r.X == x);
    CHECK(r.Y == y);
    CHECK(r.W == w);
    CHECK(r.H == h);
}

// Every widget "name" in an .arcui document.
void CollectNames(const json& widgets, std::vector<std::string>& out)
{
    for (const auto& w : widgets) {
        if (w.contains("name")) out.push_back(w["name"].get<std::string>());
        if (w.contains("children")) CollectNames(w["children"], out);
    }
}

void CollectTextKeys(const json& widgets, std::vector<std::string>& out)
{
    for (const auto& w : widgets) {
        if (w.contains("text_key")) out.push_back(w["text_key"].get<std::string>());
        if (w.contains("children")) CollectTextKeys(w["children"], out);
    }
}

std::vector<std::filesystem::path> ShippedArcui()
{
    std::vector<std::filesystem::path> files;
    for (const char* dir : { "engine/assets/ui", "game/assets/ui" })
        for (const auto& e : std::filesystem::directory_iterator(ArcbitTests::SourcePath(dir)))
            if (e.path().extension() == ".arcui") files.push_back(e.path());
    return files;
}
} // namespace

// ---------------------------------------------------------------------------
// Layout math
// ---------------------------------------------------------------------------

TEST_CASE("Widget rect: default top-left anchor and pivot", "[ui][layout]")
{
    Panel p;
    p.Size   = { 200.0f, 50.0f };
    p.Offset = { 10.0f, 20.0f };
    CheckRect(p.ComputeRect(Screen), 10.0f, 20.0f, 200.0f, 50.0f);
}

TEST_CASE("Widget rect: centered anchor and pivot", "[ui][layout]")
{
    Panel p;
    p.Size   = { 200.0f, 100.0f };
    p.Anchor = { 0.5f, 0.5f };
    p.Pivot  = { 0.5f, 0.5f };
    CheckRect(p.ComputeRect(Screen), 860.0f, 490.0f, 200.0f, 100.0f);
}

TEST_CASE("Widget rect: bottom-right corner with an inset", "[ui][layout]")
{
    Panel p;
    p.Size   = { 100.0f, 40.0f };
    p.Anchor = { 1.0f, 1.0f };
    p.Pivot  = { 1.0f, 1.0f };
    p.Offset = { -8.0f, -8.0f };
    CheckRect(p.ComputeRect(Screen), 1812.0f, 1032.0f, 100.0f, 40.0f);
}

TEST_CASE("Widget rect: SizePercent overrides Size per axis, relative to the parent", "[ui][layout]")
{
    constexpr UIRect parent{ 100.0f, 50.0f, 400.0f, 200.0f };
    Panel            p;
    p.Size        = { 30.0f, 30.0f };
    p.SizePercent = { 0.5f, 0.0f }; // half the parent's width, fixed height
    CheckRect(p.ComputeRect(parent), 100.0f, 50.0f, 200.0f, 30.0f);
}

TEST_CASE("UIRect containment is half-open", "[ui][layout]")
{
    constexpr UIRect r{ 10.0f, 10.0f, 20.0f, 20.0f };
    CHECK(r.Contains({ 10.0f, 10.0f }));
    CHECK(r.Contains({ 29.9f, 29.9f }));
    CHECK_FALSE(r.Contains({ 30.0f, 15.0f }));
    CHECK_FALSE(r.Contains({ 15.0f, 9.9f }));
}

// ---------------------------------------------------------------------------
// .arcui loading
// ---------------------------------------------------------------------------

TEST_CASE("UILoader builds the widget tree, layout fields, and meta", "[ui][arcui-format]")
{
    ArcbitTests::TempDir dir;
    const std::string    path = dir.Write("test.arcui", R"({
        "widgets": [
            { "type": "Panel", "name": "root", "size": [300, 200], "anchor": [0.5, 0.5], "pivot": [0.5, 0.5],
              "offset": [4, -6],
              "children": [
                  { "type": "Button", "name": "ok", "text": "OK", "size": [80, 24] },
                  { "type": "NotAWidget", "name": "ghost" }
              ] }
        ],
        "meta": { "spacing": 12, "title": "Options" }
    })");

    UIScreen screen;
    REQUIRE(UILoader::Load(path, screen));

    auto* root = screen.FindWidget<Panel>("root");
    REQUIRE(root != nullptr);
    CHECK(root->Size == Vec2{ 300.0f, 200.0f });
    CHECK(root->Anchor == Vec2{ 0.5f, 0.5f });
    CHECK(root->Offset == Vec2{ 4.0f, -6.0f });

    auto* ok = screen.FindWidget<Button>("ok");
    REQUIRE(ok != nullptr);
    CHECK(ok->Text == "OK");

    CHECK(screen.FindWidget<UIWidget>("ghost") == nullptr); // unknown types are skipped, not fatal
    CHECK(screen.GetMetaF32("spacing") == 12.0f);
    CHECK(screen.GetMetaStr("title") == "Options");
    CHECK(screen.GetMetaF32("absent", -1.0f) == -1.0f);
}

TEST_CASE("UILoader rejects missing and malformed files", "[ui][arcui-format]")
{
    ArcbitTests::TempDir dir;
    UIScreen             screen;
    CHECK_FALSE(UILoader::Load(dir.File("missing.arcui").string(), screen));
    CHECK_FALSE(UILoader::Load(dir.Write("bad.arcui", R"({ "widgets": [ )"), screen));
}

TEST_CASE("Every shipped .arcui loads every named widget", "[ui][arcui-format][shipped]")
{
    const auto files = ShippedArcui();
    REQUIRE_FALSE(files.empty());
    for (const auto& path : files) {
        INFO(path.string());
        UIScreen screen;
        REQUIRE(UILoader::Load(path.string(), screen));

        // A widget the loader drops (unknown type, bad field) disappears from the
        // tree; every name in the file must be findable.
        std::vector<std::string> names;
        CollectNames(json::parse(std::ifstream(path)).value("widgets", json::array()), names);
        for (const auto& name : names) {
            INFO("widget " << name);
            CHECK(screen.FindWidget<UIWidget>(name) != nullptr);
        }
    }
}

TEST_CASE("Every text_key in the shipped engine UI exists in en.json", "[ui][arcui-format][loc][shipped]")
{
    Loc::Clear();
    REQUIRE(Loc::Load(ArcbitTests::SourcePath("engine/assets/locale/en.json").string()));
    for (const auto& e : std::filesystem::directory_iterator(ArcbitTests::SourcePath("engine/assets/ui"))) {
        if (e.path().extension() != ".arcui") continue;
        std::vector<std::string> keys;
        CollectTextKeys(json::parse(std::ifstream(e.path())).value("widgets", json::array()), keys);
        for (const auto& key : keys) {
            INFO(e.path().filename().string() << ": " << key);
            CHECK(Loc::Get(key) != key);
        }
    }
    Loc::Clear();
}

// ---------------------------------------------------------------------------
// Credits
// ---------------------------------------------------------------------------

TEST_CASE("Credits lay out the title, then each section's heading and names", "[ui][credits]")
{
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "title": "Game",
        "sections": [ { "heading": "Code", "names": ["A", "B"] }, { "names": ["C"] } ] })");
    f32        height = 0.0f;
    const auto lines  = CreditsScreen::Layout(doc, height);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0].Text == "Game");
    CHECK(lines[0].Style == CreditsScreen::Line::Kind::Title);
    CHECK(lines[1].Style == CreditsScreen::Line::Kind::Heading);
    CHECK(lines[2].Text == "A");
    CHECK(lines[4].Text == "C"); // a section without a heading is just names
    for (size_t i = 1; i < lines.size(); ++i) CHECK(lines[i].Y > lines[i - 1].Y); // top to bottom
    CHECK(height > lines.back().Y);
}

TEST_CASE("Credits of something that isn't a credits document are empty", "[ui][credits]")
{
    f32 height = 1.0f;
    CHECK(CreditsScreen::Layout(nlohmann::json::array(), height).empty());
    CHECK(height == 0.0f);
}

TEST_CASE("The shipped demo credits lay out", "[ui][credits][shipped]")
{
    std::ifstream f(ArcbitTests::SourcePath("game/assets/credits.json"));
    REQUIRE(f);
    f32 height = 0.0f;
    CHECK(CreditsScreen::Layout(nlohmann::json::parse(f), height).size() > 5);
}

TEST_CASE("A credits file that is valid JSON but not an object shows the fallback", "[ui][credits]")
{
    // Regression (PR #4 review): [] or null parsed fine, then reading
    // scroll_speed threw out of OnEnter.
    const ArcbitTests::TempDir tmp;
    for (const char* text : { "[]", "null" }) {
        INFO(text);
        CreditsScreen screen;
        screen.Path = tmp.Write("credits.json", text);
        CHECK_NOTHROW(screen.OnEnter());
    }
}

// A menu column's buttons are stacked by StackButtons, which also sets the
// focus order: the arrows go down the column as drawn, whatever order the
// layout file lists them in, and hidden ones are skipped.
TEST_CASE("The pause menu's focus follows its buttons top to bottom", "[ui]")
{
    PauseMenuScreen menu;
    menu.LayoutPath     = ArcbitTests::SourcePath("engine/assets/ui/pause_menu.arcui").string();
    menu.ShowSaveGame   = true;
    menu.ShowLoadGame   = true;
    menu.OnGameSettings = [] {};
    menu.OnEnter();

    std::vector<std::string> order;
    f32                      lastY = -1.0f;
    for (int i = 0; i < 8; ++i) {
        menu.FocusNext();
        const UIWidget* focused = menu.GetFocusedWidget();
        REQUIRE(focused);
        CHECK(focused->Offset.Y > lastY);
        lastY = focused->Offset.Y;
        order.push_back(focused->Name);
    }
    CHECK(order == std::vector<std::string>{ "resume-btn", "save-btn", "load-btn", "game-btn", "controls-btn",
                                             "audio-btn", "graphics-btn", "quit-btn" });

    PauseMenuScreen fewer; // no Save / Load: the column closes up
    fewer.LayoutPath = menu.LayoutPath;
    fewer.OnEnter();
    fewer.FocusNext();
    fewer.FocusNext();
    REQUIRE(fewer.GetFocusedWidget());
    CHECK(fewer.GetFocusedWidget()->Name == "controls-btn");
}
