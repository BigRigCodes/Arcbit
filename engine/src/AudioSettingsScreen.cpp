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
// Rows
// ---------------------------------------------------------------------------

void AudioSettingsScreen::AddVolumeRow(UIWidget& panel, const AudioCategory& category, const f32 y)
{
    UIWidget* row = AddFromTemplate("volume-row", panel);
    if (!row) return;
    row->Name     = "volume-" + category.Name;
    row->Offset.Y = y;
    if (auto* label = dynamic_cast<Label*>(row->FindDescendant("label"))) {
        if (Loc::Has(category.Label)) label->SetTextKey(category.Label); // follows the language
        else label->Text = category.Label;
        label->Offset.X += GetMetaF32("indent", 24.0f) * static_cast<f32>(category.Depth > 0 ? category.Depth - 1 : 0);
    }
    WireVolumeRow(*row, category.Name);
}

void AudioSettingsScreen::WireVolumeRow(UIWidget& row, std::string category)
{
    auto* slider = dynamic_cast<Slider*>(row.FindDescendant("slider"));
    auto* pct    = dynamic_cast<Label*>(row.FindDescendant("pct"));
    auto* dec    = dynamic_cast<Button*>(row.FindDescendant("dec"));
    auto* inc    = dynamic_cast<Button*>(row.FindDescendant("inc"));

    const auto get  = [category] { return Settings::Audio.Volume(category); };
    const auto set  = [category](const f32 v) {
        Settings::Audio.Volumes[category] = v;
        Settings::MarkDirty("audio");
        AudioManager::SetVolume(category, v);
    };
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

    if (auto* panel = FindWidget<UIWidget>("bg")) {
        f32 y = GetMetaF32("row_start", 72.0f);
        for (const AudioCategory* category : AudioManager::GetCategories().InOrder()) {
            AddVolumeRow(*panel, *category, y);
            y += GetMetaF32("row_gap", 48.0f);
        }
        panel->Size.Y = y + GetMetaF32("panel_bottom", 84.0f);
    }
    if (auto* back = FindWidget<Button>("back-btn"))
        back->OnClick = [this] { if (OnBack) OnBack(); };
}

void AudioSettingsScreen::OnBackPressed()
{
    if (OnBack) OnBack();
}

} // namespace Arcbit
