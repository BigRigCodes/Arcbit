#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// HudScreen — engine-provided heads-up display.
//
// Shows health and mana bars in the bottom-left corner with an optional FPS
// counter in the top-right.  Game code updates values by writing to the
// public ProgressBar and Label pointers directly each tick.
//
// Extend in game code by subclassing and overriding OnEnter() — call the base
// first to build the standard widgets, then AddChild your own.
// ---------------------------------------------------------------------------
class HudScreen : public UIScreen
{
public:
    HudScreen()
    {
        TransitionSpeed = 0.0f; // HUDs appear instantly
        Layer           = UILayer::Hud;
        TakesInput      = false; // display only: menus above it get the keys
    }

    void OnEnter() override;

    // Pointers valid after OnEnter. Set Value (0-1) each tick.
    [[nodiscard]] ProgressBar* GetHealthBar() const { return _healthBar; }
    [[nodiscard]] ProgressBar* GetManaBar()   const { return _manaBar;   }

    // Set Visible = true and update Text each tick to show the FPS counter.
    [[nodiscard]] Label* GetFpsLabel() const { return _fpsLabel; }

private:
    ProgressBar* _healthBar = nullptr;
    ProgressBar* _manaBar   = nullptr;
    Label*       _fpsLabel  = nullptr;
};
} // namespace Arcbit
