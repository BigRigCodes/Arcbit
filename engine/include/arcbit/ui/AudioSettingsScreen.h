#pragma once

#include <arcbit/audio/AudioCategories.h>
#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>
#include <string_view>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// AudioSettingsScreen - a volume row for every audio category the game has
// (AudioManager::GetCategories: master, music, sfx, voice, ui and a game's
// own), nested ones indented. Each row is the layout's "volume-row"
// template (label, dec, slider, inc, pct), named volume-<category>; meta
// row_start / row_gap / panel_bottom / indent place them. Changes apply at
// once and go to Settings::Audio. Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class AudioSettingsScreen : public UIScreen
{
public:
    std::function<void()> OnBack;
    std::string LayoutPath = "assets/engine/ui/audio_settings.arcui";

    AudioSettingsScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnBackPressed() override;

private:
    void AddVolumeRow(UIWidget& panel, const AudioCategory& category, f32 y);
    void WireVolumeRow(UIWidget& row, std::string category);
};

} // namespace Arcbit
