// Scripts in conversations: dialog Lua (tests and statements), Dialog.Start
// / IsActive / Stop reaching the host, OnDialogEnd and "call" on the entity
// talked to, and entity:IsHeld().

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/input/InputManager.h>
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
    const Entity e                         = s.GetWorld().CreateEntity();
    s.GetWorld().GetTransform(e)->Position = at;
    s.GetWorld().AddComponent<Name>(e, Name{ name });
    return e;
}

// A scene of a player facing an NPC ("elder", scripted by elder.lua), with a
// scene script (village.lua); dialogs "started" are recorded, not run.
struct Fixture
{
    ArcbitTests::TempDir       dir;
    EventBus                   bus;
    SceneRegistry              registry;
    InputManager               input;
    WorldState                 world;
    ScriptEngine               lua{ dir.File("") };
    SceneManager               scenes;
    Lines                      out;
    std::vector<DialogRequest> started;
    bool                       active = false;
    Entity                     player, elder;

    Fixture()
    {
        RegisterBuiltinComponents(registry);
        input.RegisterAction(Interact, "Interact");
        input.BindKey(Interact, Key::Space);
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetServices({ .Scenes       = &scenes,
                          .Events       = &bus,
                          .Registry     = &registry,
                          .Assets       = [](Scene&) { return std::make_unique<NoAssets>(); },
                          .StartDialog  = [this](const DialogRequest& r) { started.push_back(r); return r.Id != "missing"; },
                          .DialogActive = [this] { return active; },
                          .StopDialog   = [this] { active = false; } });
        scenes.SetHost({ .Input    = &input,
                         .Events   = &bus,
                         .World    = &world,
                         .Registry = &registry,
                         .Assets   = [](Scene&) { return std::make_unique<NoAssets>(); },
                         .Scripts  = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
    }
    void Write(const char* name, const char* code) const { std::ofstream(dir.File(name)) << code; }
    void Go()
    {
        scenes.Register("Village", [this] {
            auto scene  = std::make_unique<LambdaScene>();
            scene->SetScript("village");
            scene->Load = [this](Scene& s) {
                player = Make(s, "player", { 0, 0 });
                s.GetWorld().AddComponent<Interactor>(player);
                s.GetWorld().AddComponent<MoveIntent>(player); // faces down, at the elder
                elder = Make(s, "elder", { 0, 30 });
                s.GetWorld().AddComponent<Interactable>(elder, Interactable{ .Prompt = "Talk" });
                s.GetWorld().AddComponent<Script>(elder, Script{ .Module = "elder" });
            };
            return scene;
        });
        scenes.Switch("Village");
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }
    Scene& Active() { return *scenes.GetActive(); }
    void   Press()
    {
        input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
        input.ProcessEdges();
        Active().Update(Dt);
        input.InjectKeyEvent(SDL_SCANCODE_SPACE, false);
        input.ProcessEdges();
    }
};

const char* Elder = R"(
local E = {}
function E:OnInteract(by) print(tostring(Dialog.Start("elder", { Node = "greet", By = by }))) end
function E:Wave() print("waves") end
function E:OnDialogEnd(id, choice) print("elder heard " .. id .. " " .. choice) end
return E
)";
const char* Village = R"(
local V = {}
function V:OnDialogEnd(id, choice, with) print("village heard " .. id .. " " .. choice .. " with " .. with.Name) end
return V
)";
} // namespace

TEST_CASE("Dialog Lua tests for truth and runs statements in its own environment", "[script][dialog]")
{
    ArcbitTests::TempDir dir;
    ScriptEngine         lua{ dir.File("") };
    int                  errors = 0;
    lua.SetErrorHandler([&](std::string_view) { ++errors; });

    CHECK(lua.Execute("met = true"));
    CHECK(lua.Test("met"));
    CHECK_FALSE(lua.Test("not met"));
    CHECK_FALSE(lua.Test("nil"));
    CHECK(lua.Test("0")); // Lua's truth: only nil and false are false
    CHECK(errors == 0);

    CHECK_FALSE(lua.Test("1 +"));         // a syntax error
    CHECK_FALSE(lua.Test("error('no')")); // a runtime error
    CHECK_FALSE(lua.Execute("x = = 1"));
    CHECK(errors == 3);
    CHECK(lua.Run("met").Values == std::vector<std::string>{ "nil" }); // not the console's globals
}

TEST_CASE("Dialog.Start from an NPC's hook talks as that NPC", "[script][dialog]")
{
    Fixture f;
    f.Write("elder.lua", Elder);
    f.Write("village.lua", "return {}");
    f.Go();
    f.Press();
    REQUIRE(f.started.size() == 1);
    const DialogRequest& r = f.started[0];
    CHECK(r.Id == "elder");
    CHECK(r.Node == "greet");
    CHECK(r.Scene == &f.Active());
    CHECK(r.With == f.elder);
    CHECK(r.By == f.player);
    CHECK(f.out == Lines{ "true" });

    CHECK(f.lua.Run("return Dialog.Start('missing')").Values == std::vector<std::string>{ "false" });
    CHECK_FALSE(f.started.back().With.IsValid()); // from the console: nobody
    f.active = true;
    CHECK(f.lua.Run("return Dialog.IsActive()").Values == std::vector<std::string>{ "true" });
    (void)f.lua.Run("Dialog.Stop()");
    CHECK_FALSE(f.active);
}

TEST_CASE("A dialog calls the NPC's methods, and its end reaches the NPC and the scene", "[script][dialog]")
{
    Fixture f;
    f.Write("elder.lua", Elder);
    f.Write("village.lua", Village);
    f.Go();
    CHECK(f.Active().CallScript(f.elder, "Wave"));
    CHECK_FALSE(f.Active().CallScript(f.elder, "Dance"));  // no such method
    CHECK_FALSE(f.Active().CallScript(f.player, "Wave")); // no script
    f.Active().DialogEnded(f.elder, "elder", "map");
    CHECK(f.out == Lines{ "waves", "elder heard elder map", "village heard elder map with elder" });
}

TEST_CASE("Scripts see whether an entity is held", "[script][dialog]")
{
    Fixture f;
    f.Write("elder.lua", Elder);
    f.Write("village.lua", "return {}");
    f.Go();
    Hold(f.Active().GetWorld(), f.elder);
    (void)f.lua.Run("print(tostring(Entity.Find('elder'):IsHeld()) .. ' ' .. tostring(Entity.Find('player'):IsHeld()))");
    CHECK(f.out == Lines{ "true false" });
}

TEST_CASE("entity:Move steers MoveIntent, and does nothing while held", "[script][dialog]")
{
    Fixture f;
    f.Write("elder.lua", Elder);
    f.Write("village.lua", "return {}");
    f.Go();
    (void)f.lua.Run("Entity.Find('elder'):Move(Vec2(3, 0))"); // gains a MoveIntent, shortened to length 1
    const MoveIntent* intent = f.Active().GetWorld().GetComponent<MoveIntent>(f.elder);
    REQUIRE(intent);
    CHECK(intent->Direction == Vec2{ 1.0f, 0.0f });

    Hold(f.Active().GetWorld(), f.elder);
    (void)f.lua.Run("Entity.Find('elder'):Move(Vec2(0, -1))");
    CHECK(f.Active().GetWorld().GetComponent<MoveIntent>(f.elder)->Facing == Vec2{ 1.0f, 0.0f }); // untouched
}
