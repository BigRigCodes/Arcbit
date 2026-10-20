#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// GameSettingsScreen - the "Game" settings: the language, as a dropdown of
// every language the game has, each named in itself ("Francais" whatever the
// current language) and filterable when there are many. Picking one switches
// at once (OnLanguage: Application::SetLanguage), and the screen redraws in
// it. Games add their own rows here - difficulty, text speed - with their own
// layout (LayoutPath) and OnBuilt to wire them (Application::OnGameSettings).
//
// The dialog rows - Text speed and Auto-advance (DialogSettings) - save as
// they change.
//
// Layout: a "language-dd" Dropdown, a "text-speed-dd" Dropdown, an
// "auto-advance" Switch and a "back-btn".
// ---------------------------------------------------------------------------
class GameSettingsScreen : public UIScreen
{
public:
    std::string LayoutPath = "assets/engine/ui/game_settings.arcui";

    std::function<void(const std::string& code)> OnLanguage;
    std::function<void()>                        OnBack;
    std::function<void(GameSettingsScreen&)>     OnBuilt; // after the layout loads and the engine's rows are wired

    GameSettingsScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 6.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override;

protected:
    void OnLocaleChanged() override { WireLanguage(); } // switched from elsewhere (the console): show it

private:
    void WireLanguage();
    void WireDialog();
};
} // namespace Arcbit
