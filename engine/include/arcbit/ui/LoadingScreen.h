#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>

namespace Arcbit
{
class Label;
class ProgressBar;

// ---------------------------------------------------------------------------
// LoadingScreen - the engine's default loading screen: "Loading" with
// animated dots and a progress bar in the bottom-right corner, over the
// black of a scene change.
//
// Application shows it while a scene change is held at black waiting for the
// next scene to load (SceneManager::WantsLoadingScreen), wires Progress to
// SceneManager::GetLoadProgress, and fades it out as the new scene fades in.
// Without Progress the bar is hidden. A game supplies its own with
// Application::SetLoadingScreen - any UIScreen; leave BlocksGame off so the
// scene change keeps running under it.
// ---------------------------------------------------------------------------
class LoadingScreen : public UIScreen
{
public:
    std::function<f32()> Progress; // 0..1; polled every tick

    LoadingScreen()
    {
        BlocksInput     = true; // nothing under it should react while loading
        TakesInput      = false;
        Layer           = UILayer::Overlay;
        TransitionSpeed = 4.0f;
    }

    void OnEnter() override;
    void OnTick(f32 dt, const InputManager& input) override;

private:
    Label*       _label = nullptr;
    ProgressBar* _bar   = nullptr;
    f32          _time  = 0.0f;
    f32          _shown = 0.0f; // the bar eases toward the real progress, so stage jumps glide

    void UpdateText();
};
} // namespace Arcbit
