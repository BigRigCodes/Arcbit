// The engine API scripts call - Entity, Scene, Tilemap, Input, Save, Event -
// against a headless SceneManager with the services Application would wire.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/save/SaveManager.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/Script.h>
#include <arcbit/script/ScriptEngine.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::ContainsSubstring;

namespace
{
using Lines = std::vector<std::string>;

constexpr f32 Dt = 1.0f / 60.0f;

class NoAssets final : public SceneAssets
{
public:
    TextureHandle      LoadTexture(std::string_view) override { return {}; }
    std::string        TexturePath(TextureHandle) const override { return {}; }
    SamplerHandle      Sampler(std::string_view) override { return {}; }
    std::string        SamplerName(SamplerHandle) const override { return {}; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(ActionID) const override { return {}; }
    bool               LoadTileMap(TileMap&, std::string_view) override { return true; }
};

class LambdaScene : public Scene
{
public:
    std::function<void(Scene&)> Load;
    void                        OnLoad() override { if (Load) Load(*this); }
};

constexpr ActionID Interact = MakeAction("Interact");

// The village: a named chest at (10, 20) with a Persistent id.
void BuildVillage(Scene& s)
{
    World&       world             = s.GetWorld();
    const Entity chest             = world.CreateEntity();
    world.GetTransform(chest)->Position = { 10, 20 };
    world.AddComponent<Name>(chest, Name{ "chest" });
    world.AddComponent<Persistent>(chest, Persistent{ .Id = "chest-1" });
}

struct Fixture
{
    ArcbitTests::TempDir    dir;
    EventBus                bus;
    SaveManager             saves{ dir.File("saves") };
    SceneRegistry           registry;
    InputManager            input;
    ScriptEngine            lua{ dir.File("") };
    SceneManager            scenes;
    Lines                   out;
    std::optional<SaveSlot> savedTo;

    Fixture()
    {
        RegisterBuiltinComponents(registry);
        input.RegisterAction(Interact, "Interact");
        input.BindKey(Interact, Key::Space);
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetServices({ .Scenes   = &scenes,
                          .Input    = &input,
                          .Events   = &bus,
                          .Saves    = &saves,
                          .Registry = &registry,
                          .Assets   = [](Scene&) { return std::make_unique<NoAssets>(); },
                          .SaveTo   = [this](const SaveSlot slot) { savedTo = slot; return true; } });
        scenes.SetHost({ .Scripts = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
        Register("Village", BuildVillage);
        Register("Cave", [](Scene&) {});
    }

    void Register(const char* name, std::function<void(Scene&)> load)
    {
        scenes.Register(name, [load] {
            auto scene  = std::make_unique<LambdaScene>();
            scene->Load = load;
            return scene;
        });
    }
    void Write(const char* name, const char* code) const { std::ofstream(dir.File(name)) << code; }
    void Go(const char* name)
    {
        scenes.Switch(name);
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }
    [[nodiscard]] std::string Eval(const char* code) // the one value it returns
    {
        const ScriptResult result = lua.Run(code);
        INFO(result.Error);
        REQUIRE(result.Ok);
        return result.Values.empty() ? "" : result.Values.front();
    }
    void Do(const char* code) // run it for its effect
    {
        const ScriptResult result = lua.Run(code);
        INFO(result.Error);
        REQUIRE(result.Ok);
    }
    World& W() { return scenes.GetActive()->GetWorld(); }
};
} // namespace

TEST_CASE("Entity.Find finds by Persistent id or Name, and handles read and move the entity", "[script]")
{
    Fixture f;
    f.Go("Village");
    CHECK(f.Eval("Entity.Find('chest-1') == Entity.Find('chest')") == "true");
    CHECK(f.Eval("Entity.Find('nobody')") == "nil");
    CHECK(f.Eval("tostring(Entity.Find('chest').Position)") == "Vec2(10, 20)");
    CHECK(f.Eval("Entity.Find('chest').Name") == "chest");

    f.Do("Entity.Find('chest').Position = Vec2(5, 6)");
    f.Do("Entity.Find('chest').Tag = 'Loot'");
    CHECK(f.Eval("Entity.Find('chest'):Get('Transform2D').position[2]") == "6.0");
    CHECK(f.Eval("Entity.FindAll('Loot')[1] == Entity.Find('chest')") == "true");
}

TEST_CASE("Components are read, written, added and removed by name", "[script]")
{
    Fixture f;
    f.Go("Village");
    f.Do("chest = Entity.Find('chest')");
    CHECK(f.Eval("chest:Has('Disabled')") == "false");
    f.Do("chest:Add('Disabled')");
    CHECK(f.Eval("chest:Has('Disabled')") == "true");
    CHECK(f.Eval("#Entity.With('Disabled', 'Name')") == "1");
    f.Do("chest:Remove('Disabled')");
    CHECK(f.Eval("chest:Has('Disabled')") == "false");

    f.Do("chest:Set('Name', { value = 'box' })"); // only the given fields change
    CHECK(f.Eval("chest.Name") == "box");
    CHECK_THAT(f.lua.Run("chest:Set('Lifetime', { seconds = 1 })").Error, ContainsSubstring("Add it first"));
    CHECK_THAT(f.lua.Run("chest:Get('NoSuchThing')").Error, ContainsSubstring("no component"));
}

TEST_CASE("Entity.Create builds from component tables; Destroy leaves handles stale", "[script]")
{
    Fixture f;
    f.Go("Village");
    f.Do("drop = Entity.Create({ Tag = { value = 'Drop' }, Transform2D = { position = { 1, 2 } } })");
    CHECK(f.Eval("tostring(drop.Position)") == "Vec2(1, 2)");
    CHECK(f.Eval("#Entity.FindAll('Drop')") == "1");
    f.Do("drop:Destroy()");
    CHECK(f.Eval("drop:IsValid()") == "false");
    CHECK(f.Eval("drop.Position") == "nil"); // stale: reads give nil
}

TEST_CASE("OnDestroy can spawn, but can't destroy or reshape its own entity", "[script]")
{
    Fixture f;
    f.Write("crate.lua", R"(
local C = {}
function C:OnDestroy()
    self.Entity:Destroy() -- already on its way out: nothing happens
    print("add ok=" .. tostring(pcall(function() self.Entity:Add("Disabled") end)))
    Entity.Create({ Tag = { value = "Splinters" }, Transform2D = { position = { self.Entity.Position.X, 0 } } })
end
return C
)");
    Entity crate;
    f.Register("Yard", [&](Scene& s) {
        crate = s.GetWorld().CreateEntity();
        s.GetWorld().GetTransform(crate)->Position = { 7, 0 };
        s.GetWorld().AddComponent<Script>(crate, Script{ .Module = "crate" });
    });
    f.Go("Yard");
    f.W().DestroyEntity(crate);
    CHECK(f.out == Lines{ "add ok=false" });
    CHECK_FALSE(f.W().IsValid(crate));
    CHECK(f.Eval("tostring(Entity.FindAll('Splinters')[1].Position)") == "Vec2(7, 0)");
}

TEST_CASE("Scene and Tilemap act on the context scene", "[script]")
{
    Fixture f;
    f.Go("Village");
    CHECK(f.Eval("Scene.GetName()") == "Village");
    f.Do("Tilemap.SetTile(2, 3, 0, 7)");
    CHECK(f.Eval("Tilemap.GetTile(2, 3)") == "7");
    CHECK(f.lua.Run("Tilemap.WorldToTile(Tilemap.TileToWorld(4, -5))").Values == Lines{ "4", "-5" });

    CHECK_THAT(f.lua.Run("Scene.Switch('Nowhere')").Error, ContainsSubstring("no scene registered"));
    f.Do("Scene.Switch('Cave', 'gate')");
    for (int i = 0; i < 5; ++i) f.scenes.Update(Dt);
    CHECK(f.Eval("Scene.GetName() .. '@' .. Scene.GetEntry()") == "Cave@gate");
}

TEST_CASE("Input reads actions by name, and nothing while the player is held still", "[script]")
{
    Fixture f;
    f.Go("Village");
    f.input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
    f.input.ProcessEdges();
    CHECK(f.Eval("Input.IsPressed('Interact')") == "true");
    CHECK(f.Eval("Input.IsPressed('Jump')") == "false"); // not an action
    f.scenes.GetActive()->SetInputBlocked(true);         // a menu is open
    CHECK(f.Eval("Input.IsPressed('Interact')") == "false");
}

TEST_CASE("Save keeps booleans, integers, decimals, text and Vec2", "[script]")
{
    Fixture f;
    f.Go("Village");
    f.Do("Save.Set('gold', 120); Save.Set('ratio', 0.5); Save.Set('quest', 'started'); Save.Set('spot', Vec2(1, 2))");
    CHECK(f.saves.Data().Get<i64>("gold", 0) == 120);
    CHECK(f.Eval("Save.Get('gold') + 1") == "121"); // still an integer
    CHECK(f.Eval("Save.Get('ratio')") == "0.5");
    CHECK(f.Eval("Save.Get('spot').Y") == "2.0");
    CHECK(f.Eval("Save.Get('missing', 'none')") == "none");
    f.Do("Save.Set('quest', nil)");
    CHECK(f.Eval("Save.Has('quest')") == "false");
    CHECK_THAT(f.lua.Run("Save.Set('bag', {})").Error, ContainsSubstring("saves keep"));

    CHECK(f.Eval("Save.To(2)") == "true");
    CHECK(f.savedTo == SaveSlot::Manual(2));
}

TEST_CASE("Event subscriptions call a function or OnEvent, and end with their script", "[script]")
{
    Fixture f;
    f.Write("listener.lua", R"(
local L = {}
function L:OnStart()
    Event.Subscribe("game.bell", function(payload) print("fn " .. payload.times) end)
    Event.Subscribe("game.bell") -- to OnEvent
end
function L:OnEvent(name, payload) print("on " .. name .. " by " .. tostring(payload.by == self.Entity)) end
return L
)");
    Entity listener;
    f.Register("Yard", [&](Scene& s) {
        listener = s.GetWorld().CreateEntity();
        s.GetWorld().AddComponent<Script>(listener, Script{ .Module = "listener" });
    });
    f.Go("Yard");

    f.bus.Publish("game.bell", { { "times", 2 }, { "by", listener } });
    f.bus.Flush();
    CHECK(f.out == Lines{ "fn 2", "on game.bell by true" });

    f.out.clear();
    f.W().DestroyEntity(listener); // its subscriptions end with it
    f.Do("Event.Publish('game.bell', { times = 3 })");
    f.bus.Flush();
    CHECK(f.out.empty());

    f.Do("Event.Publish('engine.scene.loaded', {})"); // the engine's own: refused by the bus
    CHECK(f.bus.GetQueuedCount() == 0);
}
