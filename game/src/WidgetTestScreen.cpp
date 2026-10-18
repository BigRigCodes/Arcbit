#include "WidgetTestScreen.h"

#include <arcbit/core/Loc.h>

#include <arcbit/input/InputManager.h>

#include <format>

namespace Arcbit {

void WidgetTestScreen::OnEnter()
{
    LoadLayout("assets/ui/widget_test.arcui");
    WireWidgets();
    UpdateOutput();
}

void WidgetTestScreen::OnBackPressed()
{
    if (OnClose) OnClose();
}

void WidgetTestScreen::OnTick(const f32 /*dt*/, const InputManager& /*input*/)
{
}

void WidgetTestScreen::WireWidgets()
{
    if (auto* btn = FindWidget<Button>("close-btn"))
        btn->OnClick = [this] { if (OnClose) OnClose(); };

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
