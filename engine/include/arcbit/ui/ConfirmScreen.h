#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// ConfirmScreen - a yes / no question over the current menu ("Overwrite this
// save?"). Back answers no. No has focus first, so a confirm pressed out of
// habit doesn't do the irreversible thing. The callbacks close the screen
// themselves (UIManager::Close), like every engine screen.
// ---------------------------------------------------------------------------
class ConfirmScreen : public UIScreen
{
public:
    std::string Message;
    std::string YesText; // empty = "Yes" (ui.common.yes)
    std::string NoText;  // empty = "No" (ui.common.no)

    std::function<void()> OnYes;
    std::function<void()> OnNo;

    ConfirmScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 8.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override { if (OnNo) OnNo(); }
};
} // namespace Arcbit
