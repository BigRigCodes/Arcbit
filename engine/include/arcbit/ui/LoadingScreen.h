#pragma once

#include <arcbit/ui/UIScreen.h>

namespace Arcbit
{
class Label;

// ---------------------------------------------------------------------------
// LoadingScreen - the engine's default loading screen: "Loading" with
// animated dots in the bottom-right corner, over the black of a scene change.
//
// Application shows it while a scene change is held at black waiting for the
// next scene to load (SceneManager::WantsLoadingScreen) and fades it out as
// the new scene fades in. A game supplies its own with
// Application::SetLoadingScreen - any UIScreen; leave BlocksGame off so the
// scene change keeps running under it.
// ---------------------------------------------------------------------------
class LoadingScreen : public UIScreen
{
public:
    LoadingScreen()
    {
        BlocksInput     = true; // nothing under it should react while loading
        TransitionSpeed = 4.0f;
    }

    void OnEnter() override;
    void OnTick(f32 dt, const InputManager& input) override;

private:
    Label* _label = nullptr;
    f32    _time  = 0.0f;

    void UpdateText();
};
} // namespace Arcbit
