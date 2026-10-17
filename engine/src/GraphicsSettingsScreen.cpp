#include <arcbit/ui/GraphicsSettingsScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Loc.h>

#include <format>

namespace Arcbit
{

static constexpr u32 FpsPresets[]    = {0, 30, 60, 120, 144, 165, 240};
static constexpr i32 FpsPresetCount  = static_cast<i32>(std::size(FpsPresets));

static std::string FpsPresetText(const u32 limit)
{
    return limit == 0 ? std::string(Loc::Get("ui.common.unlimited"))
                      : std::format("{} fps", limit);
}

// ---------------------------------------------------------------------------
// CycleFps
// ---------------------------------------------------------------------------

void GraphicsSettingsScreen::CycleFps(const int dir)
{
    i32 idx = 0;
    for (i32 i = 0; i < FpsPresetCount; ++i)
        if (FpsPresets[i] == Settings::Graphics.FpsLimit) { idx = i; break; }

    idx = (idx + dir + FpsPresetCount) % FpsPresetCount;
    Settings::Graphics.FpsLimit = FpsPresets[idx];
    Settings::MarkDirty("graphics");

    if (_fpsLabel) _fpsLabel->Text = FpsPresetText(Settings::Graphics.FpsLimit);
}

// ---------------------------------------------------------------------------
// OnEnter
// ---------------------------------------------------------------------------

void GraphicsSettingsScreen::OnEnter()
{
    _roots.clear();
    LoadLayout(LayoutPath);

    _fullscreenBtn   = FindWidget<Button>("fullscreen-btn");
    _vsyncSwitch     = FindWidget<Switch>("vsync-sw");
    _showFpsSwitch   = FindWidget<Switch>("show-fps-sw");
    _showDebugSwitch = FindWidget<Switch>("show-debug-sw");
    _fpsLabel        = FindWidget<Label> ("fps-val");

    if (_fullscreenBtn) {
        _fullscreenBtn->Text    = Settings::Graphics.Fullscreen
            ? Loc::Get("ui.common.on") : Loc::Get("ui.common.off");
        _fullscreenBtn->OnClick = [this]() {
            if (OnToggleFullscreen) OnToggleFullscreen();
            if (_fullscreenBtn) _fullscreenBtn->Text = Settings::Graphics.Fullscreen
                ? Loc::Get("ui.common.on") : Loc::Get("ui.common.off");
        };
    }

    if (_vsyncSwitch) {
        _vsyncSwitch->On        = Settings::Graphics.VSync;
        _vsyncSwitch->OnChanged = [](const bool on) {
            Settings::Graphics.VSync = on;
            Settings::MarkDirty("graphics");
        };
    }

    if (_showFpsSwitch) {
        _showFpsSwitch->On        = Settings::Graphics.ShowFps;
        _showFpsSwitch->OnChanged = [](const bool on) {
            Settings::Graphics.ShowFps = on;
            Settings::MarkDirty("graphics");
        };
    }

    if (_showDebugSwitch) {
        _showDebugSwitch->On        = Settings::Graphics.ShowDebugInfo;
        _showDebugSwitch->OnChanged = [](const bool on) {
            Settings::Graphics.ShowDebugInfo = on;
            Settings::MarkDirty("graphics");
        };
    }

    if (_fpsLabel) _fpsLabel->Text = FpsPresetText(Settings::Graphics.FpsLimit);
    if (auto* dec = FindWidget<Button>("fps-dec")) dec->OnClick = [this]() { CycleFps(-1); };
    if (auto* inc = FindWidget<Button>("fps-inc")) inc->OnClick = [this]() { CycleFps( 1); };

    if (auto* back = FindWidget<Button>("back-btn"))
        back->OnClick = [this] { if (OnBack) OnBack(); };
}

void GraphicsSettingsScreen::OnBackPressed()
{
    if (OnBack) OnBack();
}

} // namespace Arcbit
