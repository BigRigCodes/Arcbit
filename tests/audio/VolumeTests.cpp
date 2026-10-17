// The volume curve: settings are perceived loudness, so the gain they map to
// falls off much faster than the slider (no audio device needed).

#include <arcbit/audio/AudioManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
f32 Decibels(const f32 gain) { return 20.0f * std::log10(gain); }
} // namespace

TEST_CASE("Full volume is the authored mix and zero is silence", "[audio]")
{
    CHECK(AudioManager::SliderToGain(1.0f) == 1.0f);
    CHECK(AudioManager::SliderToGain(0.0f) == 0.0f);
    CHECK(AudioManager::SliderToGain(-0.5f) == 0.0f); // out of range clamps
    CHECK(AudioManager::SliderToGain(2.0f) == 1.0f);
}

TEST_CASE("Half volume sounds half as loud: about -10 dB", "[audio]")
{
    CHECK_THAT(Decibels(AudioManager::SliderToGain(0.5f)), WithinAbs(-10.0f, 0.1f));
}

TEST_CASE("10% volume is quiet: about -33 dB, not a linear -20 dB", "[audio]")
{
    CHECK_THAT(Decibels(AudioManager::SliderToGain(0.1f)), WithinAbs(-33.3f, 0.2f));
}

TEST_CASE("The curve only ever gets quieter as the slider goes down", "[audio]")
{
    f32 last = AudioManager::SliderToGain(1.0f);
    for (int i = 99; i >= 0; --i) {
        const f32 gain = AudioManager::SliderToGain(static_cast<f32>(i) / 100.0f);
        CHECK(gain < last);
        last = gain;
    }
}
