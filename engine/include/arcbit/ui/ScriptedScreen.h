#pragma once

#include <arcbit/ui/UIScreen.h>

#include <memory>
#include <string>

namespace Arcbit
{
class ScriptEngine;

// ---------------------------------------------------------------------------
// ScriptedScreen - a screen laid out by an .arcui file and driven by the Lua
// script it names (docs/scripting.md):
//
//   { "script": "ui.shop",
//     "widgets": [ { "type": "Button", "name": "buy", "on_click": "OnBuy" }, ... ],
//     "meta": { "layer": "hud" } }        // optional: a HUD (no input) instead of a menu
//
// The script is a module of hooks, like an entity's: OnEnter, OnExit,
// OnUpdate(dt), OnBack (default: close), and the handlers its widgets name -
// self:OnBuy(widget) for "on_click", self:OnVolume(widget, value) for
// "on_change" (sliders, switches, checkboxes, text inputs, dropdowns, radio
// groups). self.Screen finds widgets by name. Lua opens one with UI.Open;
// C++ pushes it like any screen.
// ---------------------------------------------------------------------------
class ScriptedScreen : public UIScreen
{
public:
    // Loads the layout now, so its layer is known before it's pushed.
    ScriptedScreen(ScriptEngine& engine, std::string layoutPath);
    ~ScriptedScreen() override;

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(f32 dt) override;
    void OnBackPressed() override;

    // Engine-internal: how Lua handles refer to this screen.
    [[nodiscard]] u64 GetScriptId() const;

    // Engine-internal: the script engine is going away while this screen is
    // still open - drop every reference into its Lua state now.
    void DetachScripts();

    struct Impl;

private:
    std::unique_ptr<Impl> _impl;
};
} // namespace Arcbit
