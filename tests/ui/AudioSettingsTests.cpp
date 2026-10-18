// The audio settings screen: bar sliders for any whole percent, and < >
// buttons that step to multiples of 5.

#include "TestSupport.h"

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
        Settings::Audio.MusicVolume = 0.51f;
        screen.LayoutPath           = ArcbitTests::SourcePath("engine/assets/ui/audio_settings.arcui").string();
        screen.OnEnter();
    }
    ~Fixture() { Settings::Audio = saved; }

    [[nodiscard]] i32 Music() const { return static_cast<i32>(std::round(Settings::Audio.MusicVolume * 100.0f)); }
};
} // namespace

TEST_CASE("The volume buttons step to the next multiple of 5", "[ui][audio]")
{
    Fixture f;
    auto*   dec = f.screen.FindWidget<Button>("music-dec");
    auto*   inc = f.screen.FindWidget<Button>("music-inc");
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
    CHECK(f.screen.FindWidget<Slider>("music-slider")->Value == Settings::Audio.MusicVolume);
}

TEST_CASE("Dragging a volume sets a whole percent", "[ui][audio]")
{
    Fixture f;
    auto*   slider = f.screen.FindWidget<Slider>("music-slider");
    REQUIRE(slider);
    CHECK(slider->Style == Slider::Look::Bar);
    slider->OnChanged(0.3337f);
    CHECK(Settings::Audio.MusicVolume == 0.33f);
    CHECK(f.screen.FindWidget<Label>("music-pct")->Text == "33%");
}
