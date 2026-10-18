// Scripts attached to entities and scenes, driven by a headless SceneManager:
// the lifecycle (OnLoad / OnStart / OnUpdate / OnDestroy / OnUnload),
// properties, stale handles, and errors staying contained.

#include "TestSupport.h"

#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/Script.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Lines = std::vector<std::string>;

constexpr f32 Dt = 1.0f / 60.0f;

// The scripts every test can use.
constexpr const char* Counter = R"(
local M = { Properties = { Name = "default", Step = 1 } }
function M:OnStart() print("start " .. self.Properties.Name) end
function M:OnUpdate(dt)
    self.Count = (self.Count or 0) + self.Properties.Step
    if self.Count == 3 then print(self.Properties.Name .. " counted 3") end
end
function M:OnDestroy()
    print("destroy " .. self.Properties.Name)
    M.Last = self.Entity
end
return M
)";

constexpr const char* Level = R"(
local L = {}
function L:OnLoad() print("scene load") end
function L:OnUnload() print("scene unload") end
return L
)";

constexpr const char* Broken = R"(
local B = {}
function B:OnUpdate() print("tick") error("bad") end
return B
)";

class LambdaScene : public Scene
{
public:
    std::function<void(Scene&)> Load;
    void                        OnLoad() override { if (Load) Load(*this); }
};

Entity AddScripted(Scene& scene, const char* module, nlohmann::json properties = nlohmann::json::object())
{
    const Entity e = scene.GetWorld().CreateEntity();
    scene.GetWorld().AddComponent<Script>(e, Script{ .Module = module, .Properties = std::move(properties) });
    return e;
}

struct Fixture
{
    ArcbitTests::TempDir dir;
    ScriptEngine         lua{ dir.File("") };
    SceneManager         scenes;
    Lines                out;

    Fixture()
    {
        Write("counter.lua", Counter);
        Write("level.lua", Level);
        Write("broken.lua", Broken);
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        scenes.SetHost({ .Scripts = &lua });
        scenes.FadeOutTime = 0.0f;
        scenes.FadeInTime  = 0.0f;
        scenes.Register("Empty", [] { return std::make_unique<Scene>(); });
    }

    void Write(const char* name, const char* code) const { std::ofstream(dir.File(name)) << code; }

    void Register(const char* name, std::function<void(Scene&)> load)
    {
        scenes.Register(name, [load] {
            auto scene  = std::make_unique<LambdaScene>();
            scene->Load = load;
            return scene;
        });
    }

    void Go(const char* name)
    {
        scenes.Switch(name);
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }

    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) scenes.GetActive()->Update(Dt);
    }
};
} // namespace

TEST_CASE("The scene script loads first, then each scripted entity starts and updates", "[script]")
{
    Fixture f;
    f.Register("Village", [](Scene& s) {
        s.SetScript("level");
        AddScripted(s, "counter", { { "Name", "a" } });
        AddScripted(s, "counter", { { "Name", "b" }, { "Step", 3 } });
        s.GetWorld().CreateEntity(); // no Script: runs no Lua
    });
    f.Go("Village");
    CHECK(f.out == Lines{ "scene load", "start a", "start b" });

    f.out.clear();
    f.Tick(3);
    CHECK(f.out == Lines{ "b counted 3", "a counted 3" }); // b steps by 3: its property overrides the default
}

TEST_CASE("A script added after load starts on the next update", "[script]")
{
    Fixture f;
    f.Register("Village", [](Scene&) {});
    f.Go("Village");
    AddScripted(*f.scenes.GetActive(), "counter"); // a spawned entity
    f.Tick();
    CHECK(f.out == Lines{ "start default" });
}

TEST_CASE("Destroying an entity runs OnDestroy and leaves its handles stale", "[script]")
{
    Fixture      f;
    Entity       chest;
    f.Register("Village", [&](Scene& s) { chest = AddScripted(s, "counter", { { "Name", "chest" } }); });
    f.Go("Village");
    f.scenes.GetActive()->GetWorld().DestroyEntity(chest);
    CHECK(f.out.back() == "destroy chest");
    CHECK(f.lua.Run("require('counter').Last:IsValid()").Values == std::vector<std::string>{ "false" });
    f.Tick(); // nothing left to update, nothing breaks
}

TEST_CASE("Unloading a scene ends its scripts and invalidates its handles", "[script]")
{
    Fixture f;
    f.Register("Village", [](Scene& s) {
        s.SetScript("level");
        AddScripted(s, "counter", { { "Name", "a" } });
    });
    f.Go("Village");
    f.out.clear();
    f.Go("Empty");
    CHECK(f.out == Lines{ "destroy a", "scene unload" });
    CHECK(f.lua.Run("tostring(require('counter').Last:IsValid())").Values == std::vector<std::string>{ "false" });
}

TEST_CASE("A failing OnUpdate is turned off for that script only", "[script]")
{
    Fixture f;
    f.Register("Village", [](Scene& s) {
        AddScripted(s, "broken");
        AddScripted(s, "counter", { { "Name", "ok" } });
        AddScripted(s, "missing_module"); // logged; nothing runs for it
    });
    f.Go("Village");
    f.out.clear();
    f.Tick(5);
    CHECK(f.out == Lines{ "tick", "ok counted 3" }); // broken ran once, then stayed off
}

TEST_CASE("Script components round-trip through the scene registry", "[script]")
{
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    const SceneRegistry::ComponentEntry* entry = registry.FindComponent("Script");
    REQUIRE(entry);

    struct NoAssets final : SceneAssets
    {
        TextureHandle      LoadTexture(std::string_view) override { return {}; }
        std::string        TexturePath(TextureHandle) const override { return {}; }
        SamplerHandle      Sampler(std::string_view) override { return {}; }
        std::string        SamplerName(SamplerHandle) const override { return {}; }
        const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
        std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
        std::string        ActionName(ActionID) const override { return {}; }
        bool               LoadTileMap(TileMap&, std::string_view) override { return true; }
    } assets;

    World          world;
    const Entity   e    = world.CreateEntity();
    nlohmann::json json = { { "script", "chest" }, { "properties", { { "Gold", 25 } } }, { "saved", { { "Looted", true } } } };
    entry->Read(world, e, json, assets);
    const Script* script = world.GetComponent<Script>(e);
    REQUIRE(script);
    CHECK(script->Module == "chest");
    CHECK(script->Properties["Gold"] == 25);
    CHECK(entry->WriteAll(world, e, assets) == json);
}
