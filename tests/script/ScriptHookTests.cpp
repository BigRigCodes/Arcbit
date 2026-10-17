// Hooks the world calls on scripts: OnInteract, trigger contacts (alongside
// C++ callbacks), and self.Saved surviving a scene unload through the saved
// scene state.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/physics/CollisionLayers.h>
#include <arcbit/physics/PhysicsCallbacks.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/save/SceneState.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/Script.h>
#include <arcbit/script/ScriptEngine.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Lines = std::vector<std::string>;

constexpr f32      Dt       = 1.0f / 60.0f;
constexpr ActionID Interact = MakeAction("Interact");

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

Entity Make(Scene& s, const char* name, const Vec2 at)
{
    const Entity e                      = s.GetWorld().CreateEntity();
    s.GetWorld().GetTransform(e)->Position = at;
    s.GetWorld().AddComponent<Name>(e, Name{ name });
    return e;
}

void AddCollider(Scene& s, const Entity e, const Collider2D& col)
{
    s.GetWorld().AddComponent<Collider2D>(e, col);
    (void)s.GetPhysics().RegisterCollider(e, col, s.GetWorld().GetTransform(e)->Position);
}

struct Fixture
{
    ArcbitTests::TempDir dir;
    EventBus             bus;
    SceneRegistry        registry;
    InputManager         input;
    WorldState           world;
    ScriptEngine         lua{ dir.File("") };
    SceneManager         scenes;
    Lines                out;

    Fixture()
    {
        RegisterBuiltinComponents(registry);
        input.RegisterAction(Interact, "Interact");
        input.BindKey(Interact, Key::Space);
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetServices({ .Scenes   = &scenes,
                          .Events   = &bus,
                          .Registry = &registry,
                          .Assets   = [](Scene&) { return std::make_unique<NoAssets>(); } });
        scenes.SetHost({ .Input    = &input,
                         .Events   = &bus,
                         .World    = &world,
                         .Registry = &registry,
                         .Assets   = [](Scene&) { return std::make_unique<NoAssets>(); },
                         .Scripts  = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
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
    void Tick() { scenes.GetActive()->Update(Dt); }
};
} // namespace

TEST_CASE("Interact calls the target's OnInteract and publishes the engine event", "[script]")
{
    Fixture f;
    f.Write("sign.lua", R"(
local S = {}
function S:OnInteract(by) print("read by " .. by.Name) end
return S
)");
    f.Register("Village", [](Scene& s) {
        const Entity player = Make(s, "player", { 0, 0 });
        s.GetWorld().AddComponent<Interactor>(player);
        s.GetWorld().AddComponent<MoveIntent>(player); // faces down
        const Entity sign = Make(s, "sign", { 0, 30 });
        s.GetWorld().AddComponent<Interactable>(sign, Interactable{ .Prompt = "Read" });
        s.GetWorld().AddComponent<Script>(sign, Script{ .Module = "sign" });
    });
    f.Go("Village");
    f.bus.Flush();

    EventPayload heard;
    const Subscription sub = f.bus.Subscribe(EngineEvents::EntityInteracted, [&](const Event& e) { heard = e.Payload; });
    f.input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
    f.input.ProcessEdges();
    f.Tick();
    f.bus.Flush();
    CHECK(f.out == Lines{ "read by player" });
    CHECK(heard.Has("entity"));
    CHECK(heard.Has("by"));
}

TEST_CASE("Trigger hooks reach a script alongside the entity's C++ callback", "[script]")
{
    Fixture f;
    f.Write("zone.lua", R"(
local Z = {}
function Z:OnTriggerEnter(other) print("enter " .. other.Name) end
return Z
)");
    bool cpp = false;
    f.Register("Village", [&](Scene& s) {
        const Entity zone = Make(s, "zone", { 0, 0 });
        AddCollider(s, zone, Collider2D{ .Kind = BodyKind::Static, .Layer = CollisionLayers::Trigger,
                                          .Mask = CollisionLayers::Player, .IsTrigger = true });
        s.GetWorld().AddComponent<Script>(zone, Script{ .Module = "zone" });
        s.GetWorld().AddComponent<TriggerCallback>(zone, TriggerCallback{ .OnEnter = [&](Entity, Entity) { cpp = true; } });

        const Entity hero = Make(s, "hero", { 4, 0 }); // already overlapping
        AddCollider(s, hero, Collider2D{ .Layer = CollisionLayers::Player });
    });
    f.Go("Village");
    f.Tick();
    CHECK(f.out == Lines{ "enter hero" });
    CHECK(cpp);
}

TEST_CASE("self.Saved survives leaving and coming back to the scene", "[script]")
{
    Fixture f;
    f.Write("chest.lua", R"(
local C = {}
function C:OnStart()
    print("looted=" .. tostring(self.Saved.Looted == true))
    self.Saved.Looted = true
end
return C
)");
    f.Register("Village", [](Scene& s) {
        const Entity chest = Make(s, "chest", { 0, 0 });
        s.GetWorld().AddComponent<Persistent>(chest, Persistent{ .Id = "chest" });
        s.GetWorld().AddComponent<Script>(chest, Script{ .Module = "chest" });
    });
    f.Go("Village");
    f.Go("Empty"); // captured as the village unloads
    f.Go("Village");
    CHECK(f.out == Lines{ "looted=false", "looted=true" });
    CHECK(f.world.Scenes["Village"].Entities["chest"]["Script"]["saved"]["Looted"] == true);
}
