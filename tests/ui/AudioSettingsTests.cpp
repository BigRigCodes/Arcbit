// The audio settings screen: a row per audio category (a game's own too),
// bar sliders for any whole percent, and < > buttons that step to multiples
// of 5.

#include "TestSupport.h"

#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Loc.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Arcbit;

namespace
{
// The engine's English strings, for the percentages.
struct English
{
    English()
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
    }
    ~English() { Loc::Clear(); }
};

struct Fixture
{
    English             english;
    AudioSettings       saved = Settings::Audio;
    AudioSettingsScreen screen;

    Fixture()
    {
        Settings::Audio.Volumes["music"] = 0.51f;
        screen.LayoutPath                = ArcbitTests::SourcePath("engine/assets/ui/audio_settings.arcui").string();
        screen.OnEnter();
    }
    ~Fixture() { Settings::Audio = saved; }

    [[nodiscard]] i32 Music() const { return static_cast<i32>(std::round(Settings::Audio.Volume("music") * 100.0f)); }

    // A part of a category's row: label, dec, slider, inc, pct.
    template<typename T>
    T* Part(const std::string& category, const std::string_view part)
    {
        auto* row = screen.FindWidget<UIWidget>("volume-" + category);
        return row ? dynamic_cast<T*>(row->FindDescendant(part)) : nullptr;
    }
};
} // namespace

TEST_CASE("The volume buttons step to the next multiple of 5", "[ui][audio]")
{
    Fixture f;
    auto*   dec = f.Part<Button>("music", "dec");
    auto*   inc = f.Part<Button>("music", "inc");
    REQUIRE(dec);
    REQUIRE(inc);
    dec->OnClick();
    CHECK(f.Music() == 50);
    dec->OnClick();
    CHECK(f.Music() == 45);
    inc->OnClick();
    inc->OnClick();
    CHECK(f.Music() == 55);
    for (int i = 0; i < 30; ++i) inc->OnClick();
    CHECK(f.Music() == 100);
    CHECK(f.Part<Slider>("music", "slider")->Value == Settings::Audio.Volume("music"));
    CHECK(AudioManager::GetVolume("music") == Settings::Audio.Volume("music")); // applied at once
}

TEST_CASE("Dragging a volume sets a whole percent", "[ui][audio]")
{
    Fixture f;
    auto*   slider = f.Part<Slider>("music", "slider");
    REQUIRE(slider);
    CHECK(slider->Style == Slider::Look::Bar);
    slider->OnChanged(0.3337f);
    CHECK(Settings::Audio.Volume("music") == 0.33f);
    CHECK(f.Part<Label>("music", "pct")->Text == "33%");
}

TEST_CASE("The audio screen has a row for every category, a game's own nested under its parent", "[ui][audio]")
{
    (void)AudioManager::DefineCategory("music.combat", "music", "Combat music"); // once per test run is enough
    Fixture f;
    for (const char* category : { "master", "music", "music.combat", "sfx", "voice", "ui" }) {
        INFO(category);
        CHECK(f.Part<Slider>(category, "slider"));
    }
    CHECK(f.Part<Label>("voice", "label")->Text == "Voice Volume");
    CHECK(f.Part<Label>("music.combat", "label")->Text == "Combat music");
    CHECK(f.Part<Label>("music.combat", "label")->Offset.X > f.Part<Label>("music", "label")->Offset.X); // indented
    const f32 combat = f.screen.FindWidget<UIWidget>("volume-music.combat")->Offset.Y;
    CHECK(combat > f.screen.FindWidget<UIWidget>("volume-music")->Offset.Y);
    CHECK(combat < f.screen.FindWidget<UIWidget>("volume-sfx")->Offset.Y); // right after its parent
}
