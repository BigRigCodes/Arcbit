// Audio categories: the engine's five, a game's own nested under them, and
// the volume a sound in each is heard at.

#include <arcbit/audio/AudioCategories.h>
#include <arcbit/audio/AudioManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
std::vector<std::string> Names(const AudioCategories& categories)
{
    std::vector<std::string> names;
    for (const AudioCategory* c : categories.InOrder()) names.push_back(c->Name);
    return names;
}
} // namespace

TEST_CASE("The engine's categories sit side by side under master", "[audio]")
{
    const AudioCategories categories;
    CHECK(Names(categories) == std::vector<std::string>{ "master", "music", "sfx", "voice", "ui" });
    for (const char* name : { "music", "sfx", "voice", "ui" }) CHECK(categories.Find(name)->Parent == "master");
    CHECK(categories.Find("voice")->Label == "ui.audio.voice");
}

TEST_CASE("A game's categories nest, each heard at every parent's volume", "[audio]")
{
    AudioCategories categories;
    REQUIRE(categories.Define("music.combat", "music", "Combat"));
    REQUIRE(categories.Define("ambience"));             // under master; labelled by its name
    REQUIRE(categories.Define("music.combat.drums", "music.combat"));
    CHECK_FALSE(categories.Define("music"));             // taken
    CHECK_FALSE(categories.Define("x", "nowhere"));      // no such parent
    CHECK(Names(categories) == std::vector<std::string>{ "master", "music", "music.combat", "music.combat.drums", "sfx",
                                                         "voice", "ui", "ambience" });
    CHECK(categories.Find("music.combat.drums")->Depth == 3);
    CHECK(categories.Find("ambience")->Label == "ambience");

    categories.SetVolume("master", 0.5f);
    categories.SetVolume("music", 0.5f);
    categories.SetVolume("music.combat", 0.8f);
    CHECK_THAT(categories.EffectiveVolume("music.combat.drums"), WithinAbs(0.2, 1e-6));
    CHECK_THAT(categories.EffectiveVolume("sfx"), WithinAbs(0.5, 1e-6)); // muting music leaves sfx alone
    categories.SetVolume("sfx", 0.0f);
    CHECK(categories.EffectiveVolume("voice") == 0.5f); // and SFX leaves voices alone

    // Nested groups multiply their gains; the slider-to-gain curve keeps that
    // the same as the gain of the multiplied sliders.
    CHECK_THAT(AudioManager::SliderToGain(0.5f) * AudioManager::SliderToGain(0.4f),
               WithinAbs(AudioManager::SliderToGain(0.2f), 1e-6));
}
