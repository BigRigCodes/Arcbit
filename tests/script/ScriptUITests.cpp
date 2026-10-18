// Lua-driven UI: an .arcui layout naming its script and its widgets'
// handlers, opened with UI.Open and driven through a headless UIManager.

#include "TestSupport.h"

#include <arcbit/input/InputManager.h>
#include <arcbit/script/ScriptEngine.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/ScriptedScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Lines = std::vector<std::string>;

constexpr const char* CounterLayout = R"({
  "script": "ui.counter",
  "widgets": [
    { "type": "Label",  "name": "count" },
    { "type": "Button", "name": "plus",   "text": "+1",   "on_click": "OnPlus" },
    { "type": "Slider", "name": "volume", "on_change": "OnVolume" },
    { "type": "Button", "name": "close",  "text": "Done", "on_click": "OnDone" },
    { "type": "Button", "name": "broken", "on_click": "OnBroken" }
  ]
})";

constexpr const char* CounterScript = R"(
local C = { Count = 0 }
function C:OnEnter() self.Screen:Find("count").Text = "0" end
function C:OnPlus(widget)
    C.Count = C.Count + 1
    self.Screen:Find("count").Text = tostring(C.Count)
    print("clicked " .. widget.Name)
end
function C:OnVolume(widget, value) print("volume " .. value) end
function C:OnDone() self.Screen:Close() end
function C:OnBroken() error("oops") end
function C:OnExit() print("exit") end
return C
)";

struct Fixture
{
    ArcbitTests::TempDir dir;
    UIManager            ui;
    InputManager         input;
    ScriptEngine         lua{ dir.File("") };
    Lines                out;

    Fixture()
    {
        std::filesystem::create_directories(dir.File("ui"));
        std::ofstream(dir.File("ui/counter.lua")) << CounterScript;
        std::ofstream(dir.File("counter.arcui")) << CounterLayout;
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetServices({ .UI = &ui });
    }
    [[nodiscard]] std::string Path() const { return dir.File("counter.arcui").generic_string(); }
    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) ui.Update(1.0f / 60.0f, { 640.0f, 360.0f }, input);
    }
    template<typename T>
    T* Widget(const char* name) { return ui.Top()->FindWidget<T>(name); }
};
} // namespace

TEST_CASE("UI.Open builds the layout and runs its script's hooks and handlers", "[script][ui]")
{
    Fixture f;
    REQUIRE(f.lua.Run(std::format("S = UI.Open('{}')", f.Path())).Ok);
    REQUIRE(f.ui.Top());
    CHECK(f.Widget<Label>("count")->Text == "0"); // OnEnter

    f.Widget<Button>("plus")->OnClick();
    f.Widget<Button>("plus")->OnClick();
    CHECK(f.Widget<Label>("count")->Text == "2");
    CHECK(f.lua.Run("S:Find('count').Text").Values == Lines{ "2" });

    f.Widget<Slider>("volume")->OnChanged(0.25f);
    CHECK(f.out == Lines{ "clicked plus", "clicked plus", "volume 0.25" });

    f.Widget<Button>("broken")->OnClick(); // logged, nothing thrown
    REQUIRE(f.lua.Run("S:Find('volume').Value = 0.75").Ok);
    CHECK(f.Widget<Slider>("volume")->Value == 0.75f);
}

TEST_CASE("Closing the screen runs OnExit and leaves its handles stale", "[script][ui]")
{
    Fixture f;
    REQUIRE(f.lua.Run(std::format("S = UI.Open('{}')", f.Path())).Ok);
    f.Widget<Button>("close")->OnClick();
    f.Tick(30); // the fade-out finishes
    CHECK(f.ui.Empty());
    CHECK(f.out == Lines{ "exit" });
    CHECK(f.lua.Run("S:IsOpen()").Values == Lines{ "false" });
    CHECK(f.lua.Run("S:Find('count')").Values == Lines{ "nil" });
}

TEST_CASE("A layout can ask to be a HUD instead of a menu", "[script][ui]")
{
    Fixture f;
    std::ofstream(f.dir.File("hud.arcui")) << R"({ "script": "ui.counter", "meta": { "layer": "hud" },
                                                   "widgets": [ { "type": "Label", "name": "count" } ] })";
    const ScriptedScreen hud(f.lua, f.dir.File("hud.arcui").generic_string());
    CHECK(hud.Layer == UILayer::Hud);
    CHECK_FALSE(hud.TakesInput);
    CHECK_FALSE(hud.BlocksInput);
}
