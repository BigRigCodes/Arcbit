#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderHandle.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class Image;
class Label;

// ---------------------------------------------------------------------------
// SplashEntry — one frame in the splash sequence: a full-screen image, or a
// card of centered white text on black with an optional icon above it (the
// saving notice: the saving icon, spinning, and what it means).
// ---------------------------------------------------------------------------
struct SplashEntry
{
    TextureHandle Texture;
    SamplerHandle Sampler;
    f32 Duration = 2.0f; // total display time including fades
    f32 FadeIn   = 0.5f; // fade-in duration
    f32 FadeOut  = 0.5f; // fade-out duration

    std::string   Text; // a text card's message (empty = an image entry)
    TextureHandle Icon; // above the text; white is best, Tint colors it
    SamplerHandle IconSampler;
    Color         IconTint = { 1, 1, 1, 1 };
    bool          SpinIcon = false;

    std::function<void()> OnShown; // runs as the entry starts (a notice marking itself seen)
};

// ---------------------------------------------------------------------------
// SplashScreen — plays a sequence of full-screen images before transitioning.
//
// Add SplashEntry items to Entries before pushing. Wire OnComplete to remove
// the splash (GetUI().Remove(splash) - it's on the System layer, which Pop() never touches) and
// carry on to the next screen.
//
// The screen appears and fades per-entry via each widget's Opacity — the
// screen-level transition is bypassed (TransitionSpeed = 0). Everything sits
// on black, whatever the game's clear color. Skip is triggered by Escape or
// UI_Confirm input (Enter / gamepad South).
// ---------------------------------------------------------------------------
class SplashScreen : public UIScreen
{
public:
    std::vector<SplashEntry> Entries;
    std::function<void()>    OnComplete;

    SplashScreen()
    {
        BlocksInput     = true;
        BlocksGame      = true;
        Layer           = UILayer::System; // the first scene loads (and may push a HUD) underneath
        TransitionSpeed = 0.0f;
    }

    void OnEnter()    override;
    void OnBackPressed() override;

    void OnTick(f32 dt, const InputManager& input) override;

    // Only while the current image is fully shown: a load then can't stall
    // a fade.
    [[nodiscard]] bool CoversLoading() const override;

    // Immediately finish the sequence and fire OnComplete.
    void Skip();

private:
    Image* _image        = nullptr;
    Image* _icon         = nullptr; // a text card's icon
    Label* _text         = nullptr; // a text card's message
    i32    _currentIndex = 0;
    f32    _entryTimer   = 0.0f;
    bool   _done         = false;

    void ShowEntry(i32 idx);
    void Complete();
    void SetOpacity(f32 opacity);

    [[nodiscard]] f32 ComputeOpacity() const;
};
} // namespace Arcbit
