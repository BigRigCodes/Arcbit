#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// NoticeScreen - a card that waits for OK: a title and a few lines ("Controls
// updated" / "New: Melee is F (E is your Interact)"). Confirm, a click on OK
// or back closes it; OnOk does the closing (UIManager::Close), like every
// engine screen.
// ---------------------------------------------------------------------------
class NoticeScreen : public UIScreen
{
public:
    std::string              Title;
    std::vector<std::string> Lines;
    std::function<void()>    OnOk;

    NoticeScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 8.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override { if (OnOk) OnOk(); }
};

// ---------------------------------------------------------------------------
// NoticeToast - the same notice as a box along the bottom of the screen for
// a few seconds, fading out; it takes no input. OnDone fires once it has
// faded (close it then).
// ---------------------------------------------------------------------------
class NoticeToast : public UIScreen
{
public:
    std::string              Title;
    std::vector<std::string> Lines;
    f32                      Seconds = 8.0f; // its last second fading
    std::function<void()>    OnDone;

    NoticeToast()
    {
        Layer           = UILayer::Overlay;
        TakesInput      = false;
        TransitionSpeed = 0.0f;
    }

    void OnEnter() override;
    void OnUpdate(f32 dt) override;

private:
    UIWidget* _box  = nullptr;
    f32       _age  = 0.0f;
    bool      _done = false;
};
} // namespace Arcbit
