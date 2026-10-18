// Lua-driven UI: ScriptedScreen's script side, the Screen and Widget handles
// scripts hold, and UI.Open / UI.Close.

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/ScriptedScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <format>
#include <optional>
#include <variant>

namespace Arcbit
{
// What scripts hold: a screen by id, a widget by its screen and name - looked
// up on every use, so a closed screen's handles just find nothing.
struct ScriptScreenHandle
{
    u64 Id = 0;
};

struct ScriptWidget
{
    u64         Screen = 0;
    std::string Name;
};

struct ScriptedScreen::Impl
{
    ScriptEngine::State* Lua; // null once detached (the script engine went first)
    ScriptedScreen&      Owner;
    u64                  Id;
    u64                  OwnerId; // owns the subscriptions and tasks its script starts
    std::string          Layout;
    sol::table           Self;
    sol::table           Module;
    bool                 UpdateOff  = false;
    u32                  Generation = 0; // Lua->ReloadGeneration last seen

    Impl(ScriptEngine::State& lua, ScriptedScreen& owner, std::string layout)
        : Lua(&lua), Owner(owner), Id(lua.NextScreenId++), OwnerId(lua.NextOwnerId++), Layout(std::move(layout))
    {
        Lua->Screens[Id] = &Owner;
    }
    ~Impl() { Detach(); }

    // Let go of everything in the Lua state - before it closes, or as the screen goes.
    void Detach()
    {
        if (!Lua) return;
        Lua->EndSubscriptions(OwnerId);
        Lua->Screens.erase(Id);
        Self   = {};
        Module = {};
        Lua    = nullptr;
    }

    // self:hook(args...) if the script has it; false if it failed (logged).
    template<typename... Args>
    bool CallHook(const std::string& hook, Args&&... args)
    {
        if (!Lua || !Module.valid()) return true;
        const sol::object fn = Module[hook];
        if (fn.get_type() != sol::type::function) return true;
        const ScriptEngine::State::ContextScope scope(*Lua, ScriptContext{ .Owner = OwnerId, .Self = Self });
        const sol::protected_function_result result = Lua->Call(fn.as<sol::protected_function>(), Self, std::forward<Args>(args)...);
        if (result.valid()) return true;
        Lua->ReportError(std::format("UI '{}' {}: {}", Layout, hook, result.get<sol::error>().what()));
        return false;
    }

    // A widget's handler: like a hook, but naming one the script lacks is a mistake.
    template<typename... Args>
    void CallHandler(const std::string& fn, Args&&... args)
    {
        if (Lua && Module.valid() && Module[fn].get_type() != sol::type::function)
            LOG_WARN(Scripting, "UI '{}': no function '{}' for a widget's handler", Layout, fn);
        else CallHook(fn, std::forward<Args>(args)...);
    }

    void Start();
    void Wire(const ScriptHandler& handler);
};

void ScriptedScreen::Impl::Start()
{
    const std::string module = Owner.GetMetaStr("script");
    if (module.empty()) {
        LOG_WARN(Scripting, "UI '{}' names no \"script\" - its handlers do nothing", Layout);
        return;
    }
    try {
        const sol::object loaded = Lua->RequireModule(module);
        if (loaded.get_type() != sol::type::table) throw std::runtime_error(std::format("'{}' must return a table", module));
        Module = loaded;
    }
    catch (const std::exception& e) {
        Lua->ReportError(std::format("UI '{}': {}", Layout, e.what()));
        return;
    }
    Self                     = Lua->Lua.create_table();
    Self["Screen"]           = ScriptScreenHandle{ Id };
    Self["Run"]              = Lua->SelfRun;
    sol::table meta          = Lua->Lua.create_table();
    meta["__index"]          = Module;
    Self[sol::metatable_key] = meta;
    for (const ScriptHandler& handler : Owner.GetScriptHandlers()) Wire(handler);
}

// "click" on buttons; "change" on input widgets, with the new value (a
// dropdown's or radio group's choice counts from 1, like Lua).
void ScriptedScreen::Impl::Wire(const ScriptHandler& h)
{
    const ScriptWidget widget{ Id, h.Widget->Name };
    const std::string  fn = h.Function;
    if (h.Event == "click") {
        if (auto* b = dynamic_cast<Button*>(h.Widget)) b->OnClick = [this, fn, widget] { CallHandler(fn, widget); };
        else if (auto* nb = dynamic_cast<NineSliceButton*>(h.Widget)) nb->OnClick = [this, fn, widget] { CallHandler(fn, widget); };
        else LOG_WARN(Scripting, "UI '{}': \"on_click\" on '{}', which isn't a button", Layout, h.Widget->Name);
        return;
    }
    if (auto* t = dynamic_cast<TextInput*>(h.Widget)) t->OnChanged = [this, fn, widget](const std::string& v) { CallHandler(fn, widget, v); };
    else if (auto* s = dynamic_cast<Slider*>(h.Widget)) s->OnChanged = [this, fn, widget](const f32 v) { CallHandler(fn, widget, v); };
    else if (auto* c = dynamic_cast<Checkbox*>(h.Widget)) c->OnChanged = [this, fn, widget](const bool v) { CallHandler(fn, widget, v); };
    else if (auto* w = dynamic_cast<Switch*>(h.Widget)) w->OnChanged = [this, fn, widget](const bool v) { CallHandler(fn, widget, v); };
    else if (auto* d = dynamic_cast<Dropdown*>(h.Widget)) d->OnChanged = [this, fn, widget](const i32 v) { CallHandler(fn, widget, v + 1); };
    else if (auto* r = dynamic_cast<RadioGroup*>(h.Widget)) r->OnChanged = [this, fn, widget](const i32 v) { CallHandler(fn, widget, v + 1); };
    else LOG_WARN(Scripting, "UI '{}': \"on_change\" on '{}', which has no value", Layout, h.Widget->Name);
}

// ---------------------------------------------------------------------------
// ScriptedScreen
// ---------------------------------------------------------------------------

ScriptedScreen::ScriptedScreen(ScriptEngine& engine, std::string layoutPath)
    : _impl(std::make_unique<Impl>(engine.GetState(), *this, layoutPath))
{
    TransitionSpeed = 6.0f;
    if (!LoadLayout(layoutPath)) _impl->Lua->ReportError(std::format("ScriptedScreen: can't load '{}'", layoutPath));
    const std::string layer = GetMetaStr("layer", "menu");
    if (layer == "hud" || layer == "overlay") {
        Layer           = layer == "hud" ? UILayer::Hud : UILayer::Overlay;
        TakesInput      = false;
        TransitionSpeed = 0.0f;
    }
    else BlocksInput = true;
}

ScriptedScreen::~ScriptedScreen() = default;

u64 ScriptedScreen::GetScriptId() const { return _impl->Id; }

void ScriptedScreen::DetachScripts() { _impl->Detach(); }

void ScriptedScreen::OnEnter()
{
    if (!_impl->Lua) return;
    _impl->Start();
    _impl->CallHook("OnEnter");
}

void ScriptedScreen::OnExit()
{
    _impl->CallHook("OnExit");
    if (_impl->Lua) _impl->Lua->EndSubscriptions(_impl->OwnerId);
}

void ScriptedScreen::OnUpdate(const f32 dt)
{
    if (_impl->Lua && _impl->Generation != _impl->Lua->ReloadGeneration) {
        _impl->Generation = _impl->Lua->ReloadGeneration;
        _impl->UpdateOff  = false;
    }
    if (_impl->UpdateOff || _impl->CallHook("OnUpdate", dt)) return;
    _impl->UpdateOff = true;
    LOG_WARN(Scripting, "UI '{}': OnUpdate turned off after its error", _impl->Layout);
}

// OnBack if the script has one; else the screen closes.
void ScriptedScreen::OnBackPressed()
{
    if (!_impl->Lua) return;
    if (_impl->Module.valid() && _impl->Module["OnBack"].get_type() == sol::type::function) {
        _impl->CallHook("OnBack");
        return;
    }
    if (_impl->Lua->Services.UI) _impl->Lua->Services.UI->Close(this);
}

// ---------------------------------------------------------------------------
// Lua: UI, Screen, Widget
// ---------------------------------------------------------------------------

namespace
{
    std::optional<std::string> KeyOf(UIWidget* w)
    {
        if (const auto* l = dynamic_cast<Label*>(w)) return l->TextKey;
        if (const auto* b = dynamic_cast<Button*>(w)) return b->TextKey;
        if (const auto* nb = dynamic_cast<NineSliceButton*>(w)) return nb->TextKey;
        return std::nullopt;
    }

    bool SetKey(UIWidget* w, const std::string& key)
    {
        if (auto* l = dynamic_cast<Label*>(w)) l->SetTextKey(key);
        else if (auto* b = dynamic_cast<Button*>(w)) b->SetTextKey(key);
        else if (auto* nb = dynamic_cast<NineSliceButton*>(w)) nb->SetTextKey(key);
        else return false;
        return true;
    }

    std::optional<std::string> TextOf(UIWidget* w)
    {
        if (const auto* l = dynamic_cast<Label*>(w)) return l->Text;
        if (const auto* b = dynamic_cast<Button*>(w)) return b->Text;
        if (const auto* nb = dynamic_cast<NineSliceButton*>(w)) return nb->Text;
        if (const auto* t = dynamic_cast<TextInput*>(w)) return t->Text;
        return std::nullopt;
    }

    // Text set from a script is as written: a key the widget had no longer
    // replaces it when the language changes.
    bool SetText(UIWidget* w, const std::string& text)
    {
        if (auto* l = dynamic_cast<Label*>(w)) l->Text = text, l->TextKey.clear();
        else if (auto* b = dynamic_cast<Button*>(w)) b->Text = text, b->TextKey.clear();
        else if (auto* nb = dynamic_cast<NineSliceButton*>(w)) nb->Text = text, nb->TextKey.clear();
        else if (auto* t = dynamic_cast<TextInput*>(w)) t->Text = text;
        else return false;
        return true;
    }

    // A widget's value as Lua sees it: a number (sliders, progress bars), a
    // boolean (checkboxes, switches), a 1-based choice (dropdowns, radio
    // groups; nil for none) or text (text inputs).
    sol::object ValueOf(sol::state_view lua, UIWidget* w)
    {
        if (const auto* s = dynamic_cast<Slider*>(w)) return sol::make_object(lua, s->Value);
        if (const auto* p = dynamic_cast<ProgressBar*>(w)) return sol::make_object(lua, p->Value);
        if (const auto* np = dynamic_cast<NineSliceProgressBar*>(w)) return sol::make_object(lua, np->Value);
        if (const auto* c = dynamic_cast<Checkbox*>(w)) return sol::make_object(lua, c->Checked);
        if (const auto* sw = dynamic_cast<Switch*>(w)) return sol::make_object(lua, sw->On);
        if (const auto* t = dynamic_cast<TextInput*>(w)) return sol::make_object(lua, t->Text);
        const i32 index = dynamic_cast<Dropdown*>(w)     ? static_cast<Dropdown*>(w)->SelectedIndex
                          : dynamic_cast<RadioGroup*>(w) ? static_cast<RadioGroup*>(w)->SelectedIndex
                                                         : -2;
        if (index >= 0) return sol::make_object(lua, index + 1);
        return sol::make_object(lua, sol::lua_nil);
    }

    using WidgetValue = std::variant<f64, bool, std::string>;

    // The value has to suit the widget: a number for sliders, progress bars
    // and choices, a boolean for checkboxes and switches, text for inputs.
    void SetValue(UIWidget* w, const WidgetValue& value)
    {
        const f64*         number = std::get_if<f64>(&value);
        const bool*        flag   = std::get_if<bool>(&value);
        const std::string* text   = std::get_if<std::string>(&value);
        if (auto* s = dynamic_cast<Slider*>(w); s && number) s->Value = static_cast<f32>(*number);
        else if (auto* p = dynamic_cast<ProgressBar*>(w); p && number) p->Value = static_cast<f32>(*number);
        else if (auto* np = dynamic_cast<NineSliceProgressBar*>(w); np && number) np->Value = static_cast<f32>(*number);
        else if (auto* c = dynamic_cast<Checkbox*>(w); c && flag) c->Checked = *flag;
        else if (auto* sw = dynamic_cast<Switch*>(w); sw && flag) sw->On = *flag;
        else if (auto* t = dynamic_cast<TextInput*>(w); t && text) t->Text = *text;
        else if (auto* d = dynamic_cast<Dropdown*>(w); d && number) d->SelectedIndex = static_cast<i32>(*number) - 1;
        else if (auto* r = dynamic_cast<RadioGroup*>(w); r && number) r->SelectedIndex = static_cast<i32>(*number) - 1;
        else throw std::runtime_error(std::format("Widget '{}' doesn't take that value", w->Name));
    }
} // namespace

void ScriptEngine::State::BindUI()
{
    const auto find = [this](const ScriptWidget& handle) -> UIWidget* {
        const auto it = Screens.find(handle.Screen);
        return it == Screens.end() || handle.Name.empty() ? nullptr : it->second->FindWidget<UIWidget>(handle.Name);
    };
    Lua.new_usertype<ScriptWidget>(
        "Widget", sol::no_constructor, "Name", sol::readonly_property([](const ScriptWidget& w) { return w.Name; }),
        "Text", sol::property([find](const ScriptWidget& w) { return find(w) ? TextOf(find(w)) : std::nullopt; },
                              [find](const ScriptWidget& w, const std::string& text) {
                                  if (UIWidget* widget = find(w); widget && !SetText(widget, text))
                                      throw std::runtime_error(std::format("Widget '{}' has no text", w.Name));
                              }),
        "TextKey", sol::property([find](const ScriptWidget& w) { return find(w) ? KeyOf(find(w)) : std::nullopt; },
                                 [find](const ScriptWidget& w, const std::string& key) {
                                     if (UIWidget* widget = find(w); widget && !SetKey(widget, key))
                                         throw std::runtime_error(std::format("Widget '{}' has no text key", w.Name));
                                 }),
        "Value", sol::property([this, find](const ScriptWidget& w) { return ValueOf(Lua, find(w)); },
                               [find](const ScriptWidget& w, const sol::stack_object value) {
                                   UIWidget* widget = find(w);
                                   if (!widget) return;
                                   switch (value.get_type()) {
                                       case sol::type::number: SetValue(widget, value.as<f64>()); break;
                                       case sol::type::boolean: SetValue(widget, value.as<bool>()); break;
                                       case sol::type::string: SetValue(widget, value.as<std::string>()); break;
                                       default: throw std::runtime_error("a widget's Value is a number, boolean or text");
                                   }
                               }),
        "Visible", sol::property([find](const ScriptWidget& w) { return find(w) && find(w)->Visible; },
                                 [find](const ScriptWidget& w, const bool v) { if (UIWidget* widget = find(w)) widget->Visible = v; }),
        "Enabled", sol::property([find](const ScriptWidget& w) { return find(w) && find(w)->Enabled; },
                                 [find](const ScriptWidget& w, const bool v) { if (UIWidget* widget = find(w)) widget->Enabled = v; }));

    Lua.new_usertype<ScriptScreenHandle>(
        "Screen", sol::no_constructor,
        "Find", [this](const ScriptScreenHandle& s, const std::string& name) -> std::optional<ScriptWidget> {
            const auto it = Screens.find(s.Id);
            if (it == Screens.end() || !it->second->FindWidget<UIWidget>(name)) return std::nullopt;
            return ScriptWidget{ s.Id, name };
        },
        "IsOpen", [this](const ScriptScreenHandle& s) { return Screens.contains(s.Id); },
        "Close", [this](const ScriptScreenHandle& s) {
            const auto it = Screens.find(s.Id);
            if (it != Screens.end() && Services.UI) Services.UI->Close(it->second);
        });

    sol::table ui = Lua.create_table();
    ui.set_function("Open", [this](const std::string& path) {
        if (!Services.UI) throw std::runtime_error("UI.Open: no UI");
        auto              screen = std::make_unique<ScriptedScreen>(*Engine, path);
        const ScriptScreenHandle handle{ screen->GetScriptId() };
        Services.UI->Push(std::move(screen));
        return handle;
    });
    ui.set_function("Close", [this](const ScriptScreenHandle& s) {
        const auto it = Screens.find(s.Id);
        if (it != Screens.end() && Services.UI) Services.UI->Close(it->second);
    });
    AddGlobal("UI", ui);
}
} // namespace Arcbit
