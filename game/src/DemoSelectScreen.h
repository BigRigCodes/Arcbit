#pragma once

#include "Common.h"

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// DemoSelectScreen — the demo switcher's menu (F1). One button per demo; the
// running one is highlighted. Escape or F1 closes it without switching.
// ---------------------------------------------------------------------------
class DemoSelectScreen : public Arcbit::UIScreen
{
public:
    std::function<void(size_t index)> OnPick;  // a demo button was clicked
    std::function<void()>             OnClose; // Escape / F1 without picking
    size_t                            Current = 0; // highlighted entry

    DemoSelectScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 8.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override;
    void OnTick(Arcbit::f32 dt, const Arcbit::InputManager& input) override;
};
