#include <arcbit/ui/HudScreen.h>
#include <arcbit/core/Loc.h>

namespace Arcbit
{
void HudScreen::OnEnter()
{
    _roots.clear();

    // --- HP bar container -----------------------------------------------
    // Anchored to the bottom-left corner with a small margin.
    auto* panel   = Add<Panel>();
    panel->Anchor = {0.0f, 1.0f};
    panel->Pivot  = {0.0f, 1.0f};
    panel->Offset = {12.0f, -12.0f};
    panel->Size   = {220.0f, 60.0f};
    panel->ZOrder = 0;

    // HP label + bar
    auto* hpLabel   = panel->AddChild<Label>();
    hpLabel->SetTextKey("ui.hud.hp");
    hpLabel->Size   = {24.0f, 14.0f};
    hpLabel->Anchor = {0.0f, 0.0f};
    hpLabel->Offset = {10.0f, 8.0f};
    hpLabel->ZOrder = 1;

    _healthBar                            = panel->AddChild<ProgressBar>();
    _healthBar->Size                      = {196.0f, 12.0f};
    _healthBar->Anchor                    = {0.0f, 0.0f};
    _healthBar->Offset                    = {12.0f, 24.0f};
    _healthBar->Value                     = 1.0f;
    _healthBar->SkinOverride.ProgressFill = {0.82f, 0.18f, 0.18f, 1.0f};
    _healthBar->ZOrder                    = 1;

    // MP label + bar
    auto* mpLabel   = panel->AddChild<Label>();
    mpLabel->SetTextKey("ui.hud.mp");
    mpLabel->Size   = {24.0f, 14.0f};
    mpLabel->Anchor = {0.0f, 0.0f};
    mpLabel->Offset = {10.0f, 36.0f};
    mpLabel->ZOrder = 1;

    _manaBar                            = panel->AddChild<ProgressBar>();
    _manaBar->Size                      = {196.0f, 12.0f};
    _manaBar->Anchor                    = {0.0f, 0.0f};
    _manaBar->Offset                    = {12.0f, 46.0f};
    _manaBar->Value                     = 1.0f;
    _manaBar->SkinOverride.ProgressFill = {0.22f, 0.45f, 0.90f, 1.0f};
    _manaBar->ZOrder                    = 1;

    // --- FPS label (top-right, hidden by default) -----------------------
    _fpsLabel          = Add<Label>();
    _fpsLabel->Anchor  = {1.0f, 0.0f};
    _fpsLabel->Pivot   = {1.0f, 0.0f};
    _fpsLabel->Offset  = {-10.0f, 10.0f};
    _fpsLabel->Size    = {120.0f, 20.0f};
    _fpsLabel->Align   = TextAlign::Right;
    _fpsLabel->Visible = false;
    _fpsLabel->ZOrder  = 0;
}
} // namespace Arcbit
