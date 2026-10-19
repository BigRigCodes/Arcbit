#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/InputWidgets.h>

#include <functional>
#include <string>

namespace Arcbit {

// ---------------------------------------------------------------------------
// WidgetTestScreen — interactive demo of all 6 new input widgets.
// Open with T; close with the "Close" button or Escape (when no field is
// focused; first Escape dismisses TextInput focus, second closes the screen).
// ---------------------------------------------------------------------------
class WidgetTestScreen : public UIScreen
{
public:
    std::function<void()> OnClose;
    std::function<void()> OnDialog; // the dialog box with a sample conversation

    WidgetTestScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 4.0f;
    }

protected:
    void OnLocaleChanged() override; // the composed lines follow the language
    void OnEnter() override;
    void OnBackPressed() override;
    void OnTick(f32 dt, const InputManager& input) override;

private:
    [[nodiscard]] static std::string Percent(f32 value);
    void WireWidgets();
    void UpdateOutput();

    // Mirrors the current state of each widget for the output label.
    std::string _textVal;
    std::string _numStr;
    f32         _sliderVal = 0.5f;
    i32         _dropIdx   = 1;
    bool        _checked   = false;
    i32         _radioIdx  = 1;
    bool        _switchOn  = true;

    Label* _sliderLabel = nullptr; // kept for instant slider-value feedback
};

} // namespace Arcbit
