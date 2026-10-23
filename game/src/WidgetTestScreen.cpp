#include "WidgetTestScreen.h"

#include <arcbit/core/Loc.h>

#include <arcbit/input/InputManager.h>

#include <any>
#include <array>
#include <format>

namespace Arcbit {

namespace
{
    struct Thing
    {
        const char* Key;
        Color       Tint;
        Vec2        Size; // as it lies unturned
        bool        Rotatable = false;
    };
    constexpr std::array Things = {
        Thing{ "demo.widgets.thing.potion", { 0.85f, 0.25f, 0.30f, 1.0f }, { 36.0f, 36.0f } },
        Thing{ "demo.widgets.thing.key", { 0.95f, 0.78f, 0.25f, 1.0f }, { 40.0f, 24.0f } },
        Thing{ "demo.widgets.thing.gem", { 0.30f, 0.55f, 0.95f, 1.0f }, { 24.0f, 48.0f }, true },
        Thing{ "demo.widgets.thing.boot", { 0.50f, 0.35f, 0.22f, 1.0f }, { 34.0f, 40.0f } },
        Thing{ "demo.widgets.thing.herb", { 0.35f, 0.75f, 0.35f, 1.0f }, { 30.0f, 30.0f } },
    };
    constexpr u32 KeyThing = 1;
} // namespace

void WidgetTestScreen::OnEnter()
{
    LoadLayout("assets/ui/widget_test.arcui");
    WireWidgets();
    WireDragDemo();
    UpdateOutput();
}

void WidgetTestScreen::OnBackPressed()
{
    if (OnClose) OnClose();
}

// Keyboard focus shows on the drag panel's cells: their border lights up.
void WidgetTestScreen::OnTick(const f32 /*dt*/, const InputManager& /*input*/)
{
    std::vector<UIWidget*> lit = _cellWidgets;
    lit.push_back(_discard);
    for (UIWidget* w : lit)
        if (w) w->SkinOverride.PanelBorder = w->IsFocused() ? std::optional(UISkin{}.AccentColor) : std::nullopt;
}

// The drag and drop panel: twelve cells from the layout's template, five
// things in them, and the discard box.
void WidgetTestScreen::WireDragDemo()
{
    auto* grid = FindWidget<GridPanel>("drag-grid");
    if (!grid) return;
    _cellWidgets = AddFromTemplate("drag-cell", *grid, 12);
    _cells.assign(_cellWidgets.size(), Cell{});
    for (const auto& [cell, thing] : { std::pair{ 0u, 0u }, std::pair{ 1u, 1u }, std::pair{ 2u, 2u }, std::pair{ 5u, 3u }, std::pair{ 9u, 4u } })
        if (cell < _cells.size()) _cells[cell].Thing = thing;
    for (u32 i = 0; i < _cellWidgets.size(); ++i) WireCell(i);

    _discard = FindWidget<UIWidget>("drag-discard");
    if (_discard) {
        _discard->CanDrop = [this](const DragPayload& p) { return _cells[std::any_cast<u32>(p.Data)].Thing != KeyThing; };
        _discard->OnDrop  = [this](const DragPayload& p) {
            Cell& from = _cells[std::any_cast<u32>(p.Data)];
            SetDragStatus("demo.widgets.drag.discarded", *from.Thing);
            from = {};
            ShowCells();
        };
    }
    if (auto* status = FindWidget<Label>("drag-status")) status->SetTextKey("demo.widgets.drag.ready");
    ShowCells();
}

// Each cell: a source when it holds something, a target for anything (the
// two cells swap), with its thing's name as a tooltip.
void WidgetTestScreen::WireCell(const u32 index)
{
    UIWidget* w   = _cellWidgets[index];
    w->OnDragStart = [this, index](const DragStart& how) { return PickUp(index, how); };
    w->OnDragEnd   = [this](const DragPayload& p, const bool dropped) {
        if (const auto& thing = _cells[std::any_cast<u32>(p.Data)].Thing; !dropped && thing) SetDragStatus("demo.widgets.drag.put_back", *thing);
    };
    w->CanDrop = [](const DragPayload& p) { return p.Kind == "demo-thing"; };
    w->OnDrop  = [this, index](const DragPayload& p) {
        const u32 from = std::any_cast<u32>(p.Data);
        Cell      held = _cells[from];
        held.Rotated   = p.Rotated;
        SetDragStatus(p.Count > 1 ? "demo.widgets.drag.moved_alt" : "demo.widgets.drag.moved", *held.Thing);
        _cells[from]  = _cells[index];
        _cells[index] = held;
        ShowCells();
    };
    w->GetTooltip = [this, index] { return _cells[index].Thing ? Loc::Get(Things[*_cells[index].Thing].Key) : std::string{}; };
}

std::optional<DragPayload> WidgetTestScreen::PickUp(const u32 index, const DragStart& how) const
{
    const Cell& cell = _cells[index];
    if (!cell.Thing) return std::nullopt;
    const Thing& thing = Things[*cell.Thing];
    // Count stands in for "the alternate drag" here; an inventory would split the stack.
    return DragPayload{ .Kind = "demo-thing", .Data = index, .Count = how.Alternate ? 2u : 1u, .Rotatable = thing.Rotatable,
                        .Rotated = cell.Rotated, .Tint = thing.Tint, .Size = thing.Size, .Text = how.Alternate ? "x2" : "" };
}

// Each cell's "thing" panel: the color and shape of what it holds.
void WidgetTestScreen::ShowCells()
{
    for (size_t i = 0; i < _cellWidgets.size(); ++i) {
        auto* shown = dynamic_cast<Panel*>(_cellWidgets[i]->FindDescendant("thing"));
        if (!shown) continue;
        shown->Visible = _cells[i].Thing.has_value();
        if (!shown->Visible) continue;
        const Thing& thing          = Things[*_cells[i].Thing];
        shown->SkinOverride.PanelBg = thing.Tint;
        shown->Size                 = _cells[i].Rotated ? Vec2{ thing.Size.Y, thing.Size.X } : thing.Size;
    }
}

void WidgetTestScreen::SetDragStatus(const std::string_view key, const u32 thing)
{
    if (auto* status = FindWidget<Label>("drag-status"))
        status->Text = Loc::Format(key, { { "thing", Loc::Get(Things[thing].Key) } });
}

void WidgetTestScreen::WireWidgets()
{
    if (auto* btn = FindWidget<Button>("close-btn"))
        btn->OnClick = [this] { if (OnClose) OnClose(); };
    if (auto* btn = FindWidget<Button>("dialog-btn"))
        btn->OnClick = [this] { if (OnDialog) OnDialog(); };

    if (auto* ti = FindWidget<TextInput>("text-input"))
        ti->OnChanged = [this](const std::string& v) { _textVal = v; UpdateOutput(); };

    if (auto* ni = FindWidget<TextInput>("num-input"))
        ni->OnChanged = [this](const std::string& v) { _numStr = v; UpdateOutput(); };

    _sliderLabel = FindWidget<Label>("slider-val");
    if (auto* sl = FindWidget<Slider>("volume-slider")) {
        _sliderVal = sl->Value;
        if (_sliderLabel) _sliderLabel->Text = Percent(_sliderVal); // in the language's own style ("50 %")
        sl->OnChanged = [this](const f32 v) {
            _sliderVal = v;
            if (_sliderLabel) _sliderLabel->Text = Percent(v);
            UpdateOutput();
        };
    }

    if (auto* dd = FindWidget<Dropdown>("quality-drop")) {
        _dropIdx = dd->SelectedIndex;
        dd->OnChanged = [this](const i32 idx) { _dropIdx = idx; UpdateOutput(); };
    }

    if (auto* cb = FindWidget<Checkbox>("feature-check")) {
        _checked = cb->Checked;
        cb->OnChanged = [this](const bool v) { _checked = v; UpdateOutput(); };
    }

    if (auto* rg = FindWidget<RadioGroup>("difficulty-radio")) {
        _radioIdx = rg->SelectedIndex;
        rg->OnChanged = [this](const i32 idx) { _radioIdx = idx; UpdateOutput(); };
    }

    // The typewriter: a line with effects, a pause and a slow span; Replay starts it over.
    if (auto* reveal = FindWidget<Label>("reveal-sample")) {
        reveal->StartReveal(30.0f);
        if (auto* replay = FindWidget<Button>("replay-btn")) replay->OnClick = [reveal] { reveal->StartReveal(30.0f); };
    }

    if (auto* sw = FindWidget<Switch>("autosave-switch")) {
        _switchOn = sw->On;
        sw->OnChanged = [this](const bool v) { _switchOn = v; UpdateOutput(); };
    }
}

std::string WidgetTestScreen::Percent(const f32 value)
{
    return Loc::Format("ui.common.percent", { { "value", static_cast<i32>(std::round(value * 100.0f)) } });
}

void WidgetTestScreen::OnLocaleChanged()
{
    if (_sliderLabel) _sliderLabel->Text = Percent(_sliderVal);
    UpdateOutput();
}

void WidgetTestScreen::UpdateOutput()
{
    auto* out = FindWidget<Label>("output");
    if (!out) return;

    static const char* Qualities[]    = { "demo.widgets.quality.low", "demo.widgets.quality.medium",
                                          "demo.widgets.quality.high", "demo.widgets.quality.ultra" };
    static const char* Difficulties[] = { "demo.widgets.difficulty.easy", "demo.widgets.difficulty.normal",
                                          "demo.widgets.difficulty.hard" };
    const auto pick  = [](const auto& keys, const i32 index) -> std::string {
        return index >= 0 && index < static_cast<i32>(std::size(keys)) ? Loc::Get(keys[index]) : "?";
    };
    const auto value = [](const std::string& text) { return text.empty() ? Loc::Get("demo.widgets.empty") : text; };
    const auto onOff = [](const bool on) { return Loc::Get(on ? "ui.common.on" : "ui.common.off"); };

    out->Text = Loc::Format("demo.widgets.output", { { "text", value(_textVal) },
                                                     { "number", value(_numStr) },
                                                     { "volume", Percent(_sliderVal) },
                                                     { "quality", pick(Qualities, _dropIdx) },
                                                     { "feature", onOff(_checked) },
                                                     { "difficulty", pick(Difficulties, _radioIdx) },
                                                     { "autosave", onOff(_switchOn) } });
}

} // namespace Arcbit
