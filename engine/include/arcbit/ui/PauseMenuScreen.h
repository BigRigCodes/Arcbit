#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderHandle.h>

#include <functional>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// PauseMenuScreen — engine-provided pause menu.
//
// Set PanelTexture + PanelSampler for a NineSlice background, or leave them
// invalid to fall back to a solid Panel. Wire OnResume / OnQuit before Push.
// The Controls / Audio / Graphics buttons are each independently hideable;
// set the corresponding Show* flag to false before Push to omit a button.
// Save / Load Game are off unless the game turns them on (and wires them -
// Application::ShowSaveScreen / ShowLoadScreen). Visible buttons close up.
// ---------------------------------------------------------------------------
class PauseMenuScreen : public UIScreen
{
public:
    TextureHandle PanelTexture;
    SamplerHandle PanelSampler;

    // Path to the .arcui layout file. Override to supply a custom layout.
    // If the file is absent, a built-in fallback layout is used instead.
    std::string LayoutPath = "assets/engine/ui/pause_menu.arcui";

    std::function<void()> OnResume;           // called by "Resume" button
    std::function<void()> OnSaveGame;         // called by "Save Game" button
    std::function<void()> OnLoadGame;         // called by "Load Game" button
    std::function<void()> OnControls;         // called by "Controls" button
    std::function<void()> OnAudioSettings;    // called by "Audio" button
    std::function<void()> OnGraphicsSettings; // called by "Graphics" button
    std::function<void()> OnGameSettings;     // called by "Game" button (Application::ShowGameSettingsScreen)
    std::function<void()> OnQuit;             // called by "Quit" button

    bool ShowSaveGame         = false;
    bool ShowLoadGame         = false;
    bool ShowControls         = true;
    bool ShowAudioSettings    = true;
    bool ShowGraphicsSettings = true;
    bool ShowGameSettings     = true; // shown only with OnGameSettings set

    PauseMenuScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnBackPressed() override;

private:
    [[nodiscard]] bool HasGameSettings() const { return ShowGameSettings && OnGameSettings; }
    void BuildFallback(); // inline layout used when LayoutPath file is absent
};
} // namespace Arcbit
