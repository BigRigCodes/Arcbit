#include <arcbit/ui/SavingIndicator.h>

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

} // namespace Arcbit
