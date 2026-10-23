#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/InputWidgets.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// WidgetTestScreen — interactive demo of all 6 new input widgets.
// Open with T; close with the "Close" button or Escape (when no field is
// focused; first Escape dismisses TextInput focus, second closes the screen).
// Beside it, drag and drop: things in a GridPanel to move between cells, a
// discard box that won't take the key, and tooltips.
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

    // The drag and drop panel: which thing each cell holds, turned or not.
    struct Cell
    {
        std::optional<u32> Thing;
        bool               Rotated = false;
    };
    std::vector<Cell>      _cells;
    std::vector<UIWidget*> _cellWidgets;
    UIWidget*              _discard = nullptr;
    void WireDragDemo();
    void WireCell(u32 index);
    void ShowCells();
    void SetDragStatus(std::string_view key, u32 thing);
    [[nodiscard]] std::optional<DragPayload> PickUp(u32 index, const DragStart& how) const;

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
