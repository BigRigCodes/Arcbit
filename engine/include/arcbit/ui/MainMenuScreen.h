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
// OnEnter via FindWidget. Set ShowContinue / ShowLoadGame when a save
// exists. The Controls / Audio / Graphics buttons are independently hideable;
// the visible buttons close up, leaving no gaps.
// Wire OnNewGame, OnQuit, etc. before Push.
// ---------------------------------------------------------------------------
class MainMenuScreen : public UIScreen
{
public:
    std::string LayoutPath = "assets/engine/ui/main_menu.arcui";

    std::function<void()> OnNewGame;
    std::function<void()> OnContinue;
    std::function<void()> OnLoadGame;
    std::function<void()> OnControls;
    std::function<void()> OnAudioSettings;
    std::function<void()> OnGraphicsSettings;
    std::function<void()> OnCredits;
    std::function<void()> OnLanguage; // Application::ShowLanguageScreen
    std::function<void()> OnQuit;

    bool ShowContinue         = false;
    bool ShowLoadGame         = false;
    bool ShowControls         = true;
    bool ShowAudioSettings    = true;
    bool ShowGraphicsSettings = true;
    bool ShowCredits          = false;
    bool ShowLanguage         = true; // shown only with OnLanguage set and more than one language

    MainMenuScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnBackPressed() override;

private:
    [[nodiscard]] bool HasLanguageChoice() const;
    void BuildFallback();
};
} // namespace Arcbit
