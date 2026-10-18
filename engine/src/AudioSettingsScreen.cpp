#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Loc.h>
#include <arcbit/settings/Settings.h>

#include <arcbit/ui/InputWidgets.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Arcbit
{

static std::string PctText(const f32 v)
{
    return Loc::Format("ui.common.percent", { { "value", static_cast<i32>(std::round(v * 100.0f)) } });
}

// Volumes are whole percents, so the settings file holds 0.2, not 0.19999993.
static f32 WholePercent(const f32 v) { return std::round(std::clamp(v, 0.0f, 1.0f) * 100.0f) / 100.0f; }

// < and > move to the next multiple of 5 that way: 51 -> 50, 50 -> 45, 47 -> 50.
static f32 NextStep(const f32 v, const i32 dir)
{
    constexpr i32 Step    = 5;
    const i32     percent = static_cast<i32>(std::round(v * 100.0f));
    const i32     next    = dir < 0 ? (percent - 1) / Step * Step : (percent / Step + 1) * Step;
    return static_cast<f32>(std::clamp(next, 0, 100)) / 100.0f;
}

// ---------------------------------------------------------------------------
// WireVolumeRow — find named widgets for a volume row and attach callbacks.
// ---------------------------------------------------------------------------

void AudioSettingsScreen::WireVolumeRow(const std::string_view prefix,
                                         std::function<f32()> get,
                                         std::function<void(f32)> set)
{
    const std::string p(prefix);
    auto* slider = FindWidget<Slider>(p + "-slider");
    auto* pct    = FindWidget<Label> (p + "-pct");
    auto* dec    = FindWidget<Button>(p + "-dec");
    auto* inc    = FindWidget<Button>(p + "-inc");

    const auto show = [slider, pct](const f32 v) {
        if (slider) slider->Value = v;
        if (pct) pct->Text = PctText(v);
    };
    show(get());
    // Dragging sets any whole percent; the buttons step to multiples of 5.
    if (slider) slider->OnChanged = [show, set](const f32 v) { set(WholePercent(v)); show(WholePercent(v)); };
    if (dec) dec->OnClick = [show, get, set] { set(NextStep(get(), -1)); show(get()); };
    if (inc) inc->OnClick = [show, get, set] { set(NextStep(get(), 1)); show(get()); };
}

// ---------------------------------------------------------------------------
// OnEnter
// ---------------------------------------------------------------------------

void AudioSettingsScreen::OnEnter()
{
    _roots.clear();
    LoadLayout(LayoutPath);

    WireVolumeRow("master",
        []()      { return Settings::Audio.MasterVolume; },
        [](f32 v) { Settings::Audio.MasterVolume = v; Settings::MarkDirty("audio"); AudioManager::SetMasterVolume(v); });

    WireVolumeRow("music",
        []()      { return Settings::Audio.MusicVolume; },
        [](f32 v) { Settings::Audio.MusicVolume = v; Settings::MarkDirty("audio"); AudioManager::SetMusicVolume(v); });

    WireVolumeRow("sfx",
        []()      { return Settings::Audio.SfxVolume; },
        [](f32 v) { Settings::Audio.SfxVolume = v; Settings::MarkDirty("audio"); AudioManager::SetSfxVolume(v); });

    if (auto* back = FindWidget<Button>("back-btn"))
        back->OnClick = [this] { if (OnBack) OnBack(); };
}

void AudioSettingsScreen::OnBackPressed()
{
    if (OnBack) OnBack();
}

} // namespace Arcbit
