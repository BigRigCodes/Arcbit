#include <arcbit/ui/SavingScreens.h>

#include <arcbit/core/Loc.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Arcbit
{
namespace
{
    constexpr f32 TurnsPerSecond = 0.8f;
    constexpr f32 FadeSeconds    = 0.25f;

    f32 Spin(const f32 angle, const f32 dt)
    {
        constexpr f32 Turn = 2.0f * std::numbers::pi_v<f32>;
        return std::fmod(angle + dt * TurnsPerSecond * Turn, Turn);
    }
} // namespace

// ---------------------------------------------------------------------------
// SavingIndicator
// ---------------------------------------------------------------------------

void SavingIndicator::OnEnter()
{
    _roots.clear();
    _icon          = Add<Image>();
    _icon->Texture = Icon;
    _icon->Sampler = Sampler;
    _icon->Size    = { 56.0f, 56.0f };
    _icon->Anchor  = { 1.0f, 1.0f };
    _icon->Pivot   = { 1.0f, 1.0f };
    _icon->Offset  = { -32.0f, -32.0f };

    _label         = Add<Label>();
    _label->Text   = Loc::Get("ui.saving");
    _label->Align  = TextAlign::Right;
    _label->Size   = { 220.0f, 28.0f };
    _label->Anchor = { 1.0f, 1.0f };
    _label->Pivot  = { 1.0f, 1.0f };
    _label->Offset = { -100.0f, -46.0f };
    OnUpdate(0.0f);
}

void SavingIndicator::Show(const f32 seconds)
{
    _total = std::max(seconds, 2.0f * FadeSeconds);
    _left  = _total;
}

// Fades in, holds, fades out; the icon spins throughout.
void SavingIndicator::OnUpdate(const f32 dt)
{
    _left = std::max(0.0f, _left - dt);
    _spin = Spin(_spin, dt);
    const f32 shown = _total - _left;
    const f32 alpha = _left <= 0.0f ? 0.0f : std::clamp(std::min(shown, _left) / FadeSeconds, 0.0f, 1.0f);
    if (_icon) {
        _icon->Visible  = alpha > 0.0f && Icon.IsValid();
        _icon->Rotation = _spin;
        _icon->Tint     = { Tint.R, Tint.G, Tint.B, Tint.A * alpha };
    }
    if (_label) {
        _label->Visible                = alpha > 0.0f;
        _label->SkinOverride.TextLabel = Color{ 1.0f, 1.0f, 1.0f, alpha };
    }
}

// ---------------------------------------------------------------------------
// SaveNoticeScreen
// ---------------------------------------------------------------------------

void SaveNoticeScreen::OnEnter()
{
    _roots.clear();
    constexpr f32 PanelW = 620.0f;

    Add<Scrim>();
    auto* bg   = Add<Panel>();
    bg->Size   = { PanelW, 330.0f };
    bg->Anchor = { 0.5f, 0.5f };
    bg->Pivot  = { 0.5f, 0.5f };
    bg->ZOrder = 1;

    _icon          = bg->AddChild<Image>();
    _icon->Texture = Icon;
    _icon->Sampler = Sampler;
    _icon->Tint    = Tint;
    _icon->Size    = { 72.0f, 72.0f };
    _icon->Anchor  = { 0.5f, 0.0f };
    _icon->Pivot   = { 0.5f, 0.0f };
    _icon->Offset  = { 0.0f, 30.0f };
    _icon->ZOrder  = 2;
    _icon->Visible = Icon.IsValid();

    auto* text     = bg->AddChild<Label>();
    text->Text     = Loc::Get("ui.save_notice.text");
    text->Align    = TextAlign::Center;
    text->WordWrap = true;
    text->Size     = { PanelW - 60.0f, 120.0f };
    text->Anchor   = { 0.5f, 0.0f };
    text->Pivot    = { 0.5f, 0.0f };
    text->Offset   = { 0.0f, 124.0f };
    text->ZOrder   = 2;

    auto* ok    = bg->AddChild<Button>();
    ok->Text    = Loc::Get("ui.common.ok");
    ok->Size    = { 160.0f, 44.0f };
    ok->Anchor  = { 0.5f, 1.0f };
    ok->Pivot   = { 0.5f, 1.0f };
    ok->Offset  = { 0.0f, -24.0f };
    ok->ZOrder  = 2;
    ok->OnClick = [this] { if (OnContinue) OnContinue(); };
    FocusAt(0);
}

void SaveNoticeScreen::OnUpdate(const f32 dt)
{
    if (_icon) _icon->Rotation = Spin(_icon->Rotation, dt);
}
} // namespace Arcbit
