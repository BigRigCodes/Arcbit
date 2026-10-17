#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/settings/Settings.h>

#include <algorithm>
#include <format>

namespace Arcbit
{

static std::string PctText(const f32 v)
{
    return std::format("{}%", static_cast<i32>(v * 100.0f + 0.5f));
}

// ---------------------------------------------------------------------------
// WireVolumeRow — find named widgets for a volume row and attach callbacks.
// ---------------------------------------------------------------------------

void AudioSettingsScreen::WireVolumeRow(const std::string_view prefix,
                                         std::function<f32()> get,
                                         std::function<void(f32)> set)
{
    const std::string p(prefix);
    auto* bar = FindWidget<ProgressBar>(p + "-bar");
    auto* pct = FindWidget<Label>      (p + "-pct");
    auto* dec = FindWidget<Button>     (p + "-dec");
    auto* inc = FindWidget<Button>     (p + "-inc");

    if (bar) bar->Value = get();
    if (pct) pct->Text  = PctText(get());

    auto step = [bar, pct, get, set](const f32 dir) {
        const f32 v = std::clamp(get() + dir, 0.0f, 1.0f);
        set(v);
        if (bar) bar->Value = v;
        if (pct) pct->Text  = PctText(v);
    };
    if (dec) dec->OnClick = [step]() { step(-0.1f); };
    if (inc) inc->OnClick = [step]() { step( 0.1f); };
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
