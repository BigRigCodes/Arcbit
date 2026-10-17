#include <arcbit/ui/LoadingScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/ui/Widgets.h>

namespace Arcbit
{
namespace
{
    constexpr f32 DotSeconds = 0.4f; // one more dot every DotSeconds, up to three
    constexpr f32 Margin     = 40.0f;
} // namespace

void LoadingScreen::OnEnter()
{
    _roots.clear();
    _time = 0.0f;

    // Left-aligned in a fixed box, so the word stays put as the dots grow.
    _label                         = Add<Label>();
    _label->Align                  = TextAlign::Left;
    _label->Size                   = { 220.0f, 40.0f };
    _label->Anchor                 = { 1.0f, 1.0f };
    _label->Pivot                  = { 1.0f, 1.0f };
    _label->Offset                 = { -Margin, -Margin };
    _label->SkinOverride.FontScale = 1.25f;
    UpdateText();
}

void LoadingScreen::OnTick(const f32 dt, const InputManager& /*input*/)
{
    _time += dt;
    UpdateText();
}

void LoadingScreen::UpdateText()
{
    const int dots = static_cast<int>(_time / DotSeconds) % 4;
    _label->Text   = Loc::Get("ui.loading") + std::string(dots, '.');
}
} // namespace Arcbit
