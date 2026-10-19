// Text styles in skins and layouts: JSON both ways, and per-widget overrides.

#include "TestSupport.h"

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace Arcbit;

TEST_CASE("A text style reads from JSON and writes back the same", "[ui][textstyle]")
{
    const auto json = nlohmann::json::parse(R"({ "bold": true, "outline": 3, "outline_color": [1, 0, 0, 1],
                                                 "shadow": true, "shadow_offset": [1, 4], "shadow_softness": 2 })");
    const TextStyle style = ReadTextStyle(json, {});
    CHECK(style.Bold);
    CHECK_FALSE(style.Italic);
    CHECK(style.Outline);
    CHECK(style.OutlineWidth == 3.0f);
    CHECK(style.OutlineColor == Color{ 1, 0, 0, 1 });
    CHECK(style.Shadow);
    CHECK(style.ShadowOffset == Vec2{ 1, 4 });
    CHECK(style.ShadowSoftness == 2.0f);
    CHECK(ReadTextStyle(TextStyleToJson(style), {}) == style);

    CHECK_FALSE(ReadTextStyle(nlohmann::json::parse(R"({ "outline": 0 })"), style).Outline);
    CHECK(ReadTextStyle(nlohmann::json::parse(R"({ "outline": true })"), {}).Outline);
}

TEST_CASE("Skins and layouts carry text styles", "[ui][textstyle]")
{
    ArcbitTests::TempDir dir;
    UISkin               skin;
    skin.Text.Italic = true;
    skin.Text.Shadow = true;
    REQUIRE(skin.SaveToFile(dir.File("skin.json").string()));
    const UISkin loaded = UISkin::LoadFromFile(dir.File("skin.json").string());
    CHECK(loaded.Text == skin.Text);

    UIScreen screen;
    REQUIRE(screen.LoadLayout(dir.Write("layout.arcui", R"({ "widgets": [
        { "type": "Label", "name": "title", "text": "Hi", "text_style": { "bold": true, "outline": 2 } } ] })")));
    auto* title = screen.FindWidget<Label>("title");
    REQUIRE(title);
    REQUIRE(title->SkinOverride.Text.has_value());
    CHECK(title->SkinOverride.Text->Bold);
    const UISkin effective = title->GetEffectiveSkin(skin);
    CHECK(effective.Text.Bold);
    CHECK(effective.Text.Outline);
    CHECK_FALSE(effective.Text.Italic); // an override replaces the skin's style whole
}
