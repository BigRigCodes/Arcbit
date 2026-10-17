#include <arcbit/ui/LoadingScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>

namespace Arcbit
{
namespace
{
    constexpr f32 DotSeconds = 0.4f;   // one more dot every DotSeconds, up to three
    constexpr f32 Margin     = 40.0f;
    constexpr f32 BarWidth   = 320.0f;
    constexpr f32 BarHeight  = 10.0f;
    constexpr f32 EaseRate   = 8.0f;   // per second: how fast the bar closes on the real value
} // namespace

void LoadingScreen::OnEnter()
{
    _roots.clear();
    _time  = 0.0f;
    _shown = Progress ? Progress() : 0.0f;

    _bar          = Add<ProgressBar>();
    _bar->Size    = { BarWidth, BarHeight };
    _bar->Anchor  = { 1.0f, 1.0f };
    _bar->Pivot   = { 1.0f, 1.0f };
    _bar->Offset  = { -Margin, -Margin };
    _bar->Value   = _shown;
    _bar->Visible = static_cast<bool>(Progress);

    // Left-aligned in a fixed box above the bar, so the word stays put as
    // the dots grow.
    _label                         = Add<Label>();
    _label->Align                  = TextAlign::Left;
    _label->Size                   = { BarWidth, 40.0f };
    _label->Anchor                 = { 1.0f, 1.0f };
    _label->Pivot                  = { 1.0f, 1.0f };
    _label->Offset                 = { -Margin, -Margin - (Progress ? BarHeight + 8.0f : 0.0f) };
    _label->SkinOverride.FontScale = 1.25f;
    UpdateText();
}

void LoadingScreen::OnTick(const f32 dt, const InputManager& /*input*/)
{
    _time += dt;
    UpdateText();
    if (!Progress) return;
    const f32 target = std::clamp(Progress(), 0.0f, 1.0f);
    _shown           = std::max(_shown, _shown + (target - _shown) * std::min(1.0f, dt * EaseRate)); // never backwards
    _bar->Value      = _shown;
}

void LoadingScreen::UpdateText()
{
    const int dots = static_cast<int>(_time / DotSeconds) % 4;
    _label->Text   = Loc::Get("ui.loading") + std::string(dots, '.');
}
} // namespace Arcbit
