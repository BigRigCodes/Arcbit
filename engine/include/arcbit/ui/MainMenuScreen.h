#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
class Button;

// ---------------------------------------------------------------------------
// MainMenuScreen — engine-provided main menu.
//
// Visual layout is loaded from LayoutPath (.arcui); callbacks are wired in
// OnEnter via FindWidget. Set ShowContinue = true when a save exists.
// The Controls / Audio / Graphics buttons are independently hideable.
// Wire OnNewGame, OnQuit, etc. before Push.
// ---------------------------------------------------------------------------
class MainMenuScreen : public UIScreen
{
public:
    std::string LayoutPath = "assets/engine/ui/main_menu.arcui";

    std::function<void()> OnNewGame;
    std::function<void()> OnContinue;
    std::function<void()> OnControls;
    std::function<void()> OnAudioSettings;
    std::function<void()> OnGraphicsSettings;
    std::function<void()> OnCredits;
    std::function<void()> OnQuit;

    bool ShowContinue         = false;
    bool ShowControls         = true;
    bool ShowAudioSettings    = true;
    bool ShowGraphicsSettings = true;
    bool ShowCredits          = false;

    MainMenuScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnBackPressed() override;

private:
    void BuildFallback();
};
} // namespace Arcbit
