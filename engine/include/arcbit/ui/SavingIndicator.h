#pragma once

#include <arcbit/render/RenderHandle.h>
#include <arcbit/ui/UIScreen.h>

namespace Arcbit
{
class Image;
class Label;

// ---------------------------------------------------------------------------
// SavingIndicator - the spinning icon and "Saving..." in the bottom-right
// corner while the game saves. Display only, above menus (Overlay); it sits
// on the stack the whole time, hidden until Show. Application owns it
// (SavingConfig picks the icon) and shows it on every save.
// ---------------------------------------------------------------------------
class SavingIndicator : public UIScreen
{
public:
    TextureHandle Icon; // white is best: Tint colors it
    SamplerHandle Sampler;
    Color         Tint = { 1, 1, 1, 1 };

    SavingIndicator()
    {
        Layer           = UILayer::Overlay;
        TakesInput      = false;
        TransitionSpeed = 0.0f;
    }

    // Show for `seconds`, fades included; showing again restarts it.
    void Show(f32 seconds);

    [[nodiscard]] bool IsShowing() const { return _left > 0.0f; }

    void OnEnter() override;
    void OnUpdate(f32 dt) override;

private:
    Image* _icon  = nullptr;
    Label* _label = nullptr;
    f32    _left  = 0.0f; // seconds still to show
    f32    _total = 0.0f;
    f32    _spin  = 0.0f;
};
} // namespace Arcbit
