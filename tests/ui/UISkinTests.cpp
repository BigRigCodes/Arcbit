#include "TestSupport.h"

#include <arcbit/ui/UISkin.h>

#include <catch2/catch_test_macros.hpp>

#include <utility>

using namespace Arcbit;

namespace
{
// Every color field, so tests can't silently miss one.
constexpr std::pair<const char*, Color UISkin::*> SkinColors[] = {
    { "PanelBg", &UISkin::PanelBg },
    { "PanelBorder", &UISkin::PanelBorder },
    { "ButtonNormal", &UISkin::ButtonNormal },
    { "ButtonHovered", &UISkin::ButtonHovered },
    { "ButtonPressed", &UISkin::ButtonPressed },
    { "ButtonDisabled", &UISkin::ButtonDisabled },
    { "TextNormal", &UISkin::TextNormal },
    { "TextDisabled", &UISkin::TextDisabled },
    { "TextLabel", &UISkin::TextLabel },
    { "ProgressBg", &UISkin::ProgressBg },
    { "ProgressFill", &UISkin::ProgressFill },
    { "ScrollTrack", &UISkin::ScrollTrack },
    { "ScrollThumb", &UISkin::ScrollThumb },
    { "ScrollThumbHovered", &UISkin::ScrollThumbHovered },
    { "AccentColor", &UISkin::AccentColor },
    { "ScrimColor", &UISkin::ScrimColor },
    { "InputBg", &UISkin::InputBg },
    { "InputBorder", &UISkin::InputBorder },
    { "InputFocusBorder", &UISkin::InputFocusBorder },
    { "InputCursor", &UISkin::InputCursor },
    { "InputPlaceholder", &UISkin::InputPlaceholder },
    { "SliderThumb", &UISkin::SliderThumb },
    { "SliderThumbHovered", &UISkin::SliderThumbHovered },
    { "CheckboxBg", &UISkin::CheckboxBg },
    { "CheckboxHovered", &UISkin::CheckboxHovered },
    { "CheckboxCheck", &UISkin::CheckboxCheck },
    { "SwitchOn", &UISkin::SwitchOn },
    { "SwitchOff", &UISkin::SwitchOff },
    { "SwitchThumb", &UISkin::SwitchThumb },
};

void CheckSameColors(const UISkin& actual, const UISkin& expected)
{
    for (const auto& [name, member] : SkinColors) {
        INFO(name);
        CHECK(actual.*member == expected.*member);
    }
}
} // namespace

TEST_CASE("Skin JSON sections map onto UISkin fields", "[ui][skin-format]")
{
    ArcbitTests::TempDir dir;
    const UISkin         skin = UISkin::LoadFromFile(dir.Write("s.json", R"({
        "fontScale": 1.5,
        "panel":    { "background": [0.1, 0.2, 0.3, 0.4] },
        "button":   { "pressed": [1, 0, 0, 1] },
        "input":    { "focusBorder": [0, 1, 0, 1] },
        "slider":   { "thumbHovered": [0, 0, 1, 1] },
        "checkbox": { "check": [1, 1, 0, 1] },
        "switch":   { "on": [0, 1, 1, 1] },
        "accentColor": [0.5, 0.5, 0.5, 1],
        "sounds": { "activate": "click", "toggle": "flip" }
    })"));

    CHECK(skin.FontScale == 1.5f);
    CHECK(skin.PanelBg == Color{ 0.1f, 0.2f, 0.3f, 0.4f });
    CHECK(skin.ButtonPressed == Color::Red());
    CHECK(skin.InputFocusBorder == Color::Green());
    CHECK(skin.SliderThumbHovered == Color::Blue());
    CHECK(skin.CheckboxCheck == Color::Yellow());
    CHECK(skin.SwitchOn == Color::Cyan());
    CHECK(skin.AccentColor == Color{ 0.5f, 0.5f, 0.5f, 1.0f });
    CHECK(skin.SoundActivate == "click");
    CHECK(skin.SoundToggle == "flip");
    CHECK(skin.SoundBack.empty());

    CHECK(skin.PanelBorder == UISkin::Default().PanelBorder); // untouched keys keep defaults
}

TEST_CASE("Skins round-trip through SaveToFile / LoadFromFile", "[ui][skin-format]")
{
    // Regression: input / slider / checkbox / switch colors were neither saved nor loaded.
    UISkin custom;
    f32    shade = 0.0f;
    for (const auto& [name, member] : SkinColors) {
        shade += 0.03125f; // distinct, exactly representable value per field
        custom.*member = Color{ shade, 1.0f - shade, shade * 0.5f, 1.0f };
    }
    custom.FontScale      = 0.75f;
    custom.SoundFocusMove = "move";
    custom.SoundBack      = "back";

    ArcbitTests::TempDir dir;
    const std::string    path = dir.File("skin.json").string();
    REQUIRE(custom.SaveToFile(path));
    const UISkin loaded = UISkin::LoadFromFile(path);

    CheckSameColors(loaded, custom);
    CHECK(loaded.FontScale == 0.75f);
    CHECK(loaded.SoundFocusMove == "move");
    CHECK(loaded.SoundBack == "back");
}

TEST_CASE("Bad skin input falls back to defaults", "[ui][skin-format]")
{
    ArcbitTests::TempDir dir;
    CheckSameColors(UISkin::LoadFromFile(dir.File("missing.json").string()), UISkin::Default());
    CheckSameColors(UISkin::LoadFromFile(dir.Write("bad.json", "{ \"panel\": ")), UISkin::Default());

    // A color array that's too short keeps the default for that field only.
    const UISkin shortColor = UISkin::LoadFromFile(dir.Write("short.json", R"({ "panel": { "background": [1, 0, 0] } })"));
    CHECK(shortColor.PanelBg == UISkin::Default().PanelBg);
}

TEST_CASE("The legacy overlayColor key still sets the scrim", "[ui][skin-format]")
{
    ArcbitTests::TempDir dir;
    CHECK(UISkin::LoadFromFile(dir.Write("legacy.json", R"({ "overlayColor": [0, 0, 0, 0.5] })")).ScrimColor
          == Color{ 0.0f, 0.0f, 0.0f, 0.5f });
    // scrimColor wins when both are present.
    CHECK(UISkin::LoadFromFile(dir.Write("both.json", R"({ "overlayColor": [0, 0, 0, 0.5], "scrimColor": [1, 1, 1, 1] })"))
              .ScrimColor == Color::White());
}

TEST_CASE("The shipped default skin matches the built-in defaults", "[ui][skin-format][shipped]")
{
    CheckSameColors(UISkin::LoadFromFile(ArcbitTests::SourcePath("engine/assets/skins/default.skin.json").string()),
                    UISkin::Default());
}
