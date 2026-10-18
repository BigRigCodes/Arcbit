// The Scripted demo's real scene file and Lua scripts, headless: everything
// loads and runs without a script error, and the chest, guard and Lua menu do
// what they say. Catches a demo script broken by an API change.

#include "TestSupport.h"

#include <arcbit/app/DebugConsole.h>
#include <arcbit/core/Loc.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/save/SaveManager.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneFile.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/ScriptEngine.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Arcbit;
using nlohmann::json;

namespace
{
constexpr f32 Dt = 1.0f / 60.0f;

class DemoAssets final : public SceneAssets
{
public:
    TextureHandle      LoadTexture(std::string_view) override { return {}; }
    std::string        TexturePath(TextureHandle) const override { return {}; }
    SamplerHandle      Sampler(std::string_view) override { return {}; }
    std::string        SamplerName(SamplerHandle) const override { return {}; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(const ActionID action) const override
    {
        for (const char* name : { "Move_Left", "Move_Right", "Move_Up", "Move_Down" })
            if (MakeAction(name) == action) return name;
        return {};
    }
    bool LoadTileMap(TileMap& tilemap, const std::string_view path) override
    {
        std::ifstream f(ArcbitTests::SourcePath("game") / path);
        return f && tilemap.LoadTilesFromJson(json::parse(f));
    }
};

// The scene file, loaded the way FileScene does (minus the GPU).
class DemoScene final : public Scene
{
public:
    explicit DemoScene(const SceneRegistry& registry) : _registry(registry) {}
    void OnLoad() override
    {
        std::ifstream f(ArcbitTests::SourcePath("game/assets/scenes/scripted.arcscene"));
        const json    doc = json::parse(f);
        DemoAssets    assets;
        REQUIRE(SceneFile::Load(*this, doc, _registry, assets, Ids));
        SetScript(doc.at("script").get<std::string>());
    }
    std::unordered_map<std::string, Entity> Ids;

private:
    const SceneRegistry& _registry;
};

// The script's UI paths are relative to the game's folder, as in a build.
struct InGameFolder
{
    std::filesystem::path Was = std::filesystem::current_path();
    InGameFolder() { std::filesystem::current_path(ArcbitTests::SourcePath("game")); }
    ~InGameFolder() { std::filesystem::current_path(Was); }
};

// The engine's and the demo's strings, English, for the length of a test.
struct ShippedLocales
{
    ShippedLocales()
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        Loc::AddFolder(ArcbitTests::SourcePath("game/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
    }
    ~ShippedLocales() { Loc::Clear(); }
};

struct Fixture
{
    ShippedLocales           locales; // first: the scripts and layouts read strings as they load
    InGameFolder             folder;
    ArcbitTests::TempDir     dir;
    EventBus                 bus;
    SaveManager              saves{ dir.File("saves") };
    SceneRegistry            registry;
    InputManager             input;
    UIManager                ui;
    DebugConsole             console;
    ScriptEngine             lua{ ArcbitTests::SourcePath("game/assets/scripts") };
    SceneManager             scenes;
    std::vector<std::string> errors;

    Fixture()
    {
        RegisterBuiltinComponents(registry);
        for (const auto& [name, key] : { std::pair{ "Interact", Key::E }, std::pair{ "Demo_LuaMenu", Key::L } }) {
            input.RegisterAction(MakeAction(name), name);
            input.BindKey(MakeAction(name), key);
        }
        lua.SetErrorHandler([this](const std::string_view e) { errors.emplace_back(e); });
        lua.SetServices({ .Scenes   = &scenes,
                          .Input    = &input,
                          .Events   = &bus,
                          .Saves    = &saves,
                          .Registry = &registry,
                          .UI       = &ui,
                          .Console  = &console,
                          .Assets   = [](Scene&) { return std::make_unique<DemoAssets>(); } });
        scenes.SetHost({ .Input = &input, .Events = &bus, .Scripts = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
        scenes.Register("Scripted", [this] { return std::make_unique<DemoScene>(registry); });
        scenes.Switch("Scripted");
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }
    DemoScene& Demo() { return *static_cast<DemoScene*>(scenes.GetActive()); }
    void       Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) {
            scenes.GetActive()->Update(Dt);
            bus.Flush();
            ui.Update(Dt, { 1920.0f, 1080.0f }, input);
            input.ProcessEdges();
        }
    }
    void Press(const SDL_Scancode key)
    {
        input.InjectKeyEvent(key, true);
        input.ProcessEdges();
        scenes.GetActive()->Update(Dt);
        bus.Flush();
        ui.Update(Dt, { 1920.0f, 1080.0f }, input);
        input.InjectKeyEvent(key, false);
        input.ProcessEdges();
    }
    template<typename T>
    T* Find(const char* name)
    {
        for (UIScreen* screen = ui.Top(); screen;) return screen->FindWidget<T>(name);
        return nullptr;
    }
};
} // namespace

TEST_CASE("The Scripted demo: the chest pays out once and the HUD shows it", "[script][demo]")
{
    Fixture f;
    f.Tick();
    REQUIRE(f.ui.Top());
    CHECK(f.Find<Label>("gold")->Text == "Gold: 0");

    // Stand below the chest, facing it.
    World&       world  = f.Demo().GetWorld();
    const Entity player = f.Demo().Ids.at("player");
    world.GetTransform(player)->Position = { 384.0f, -128.0f };
    world.GetComponent<MoveIntent>(player)->Facing = { 0.0f, -1.0f };
    f.Press(SDL_SCANCODE_E);
    f.Tick();
    CHECK(f.saves.Data().Get<i64>("gold", 0) == 25);
    CHECK(f.Find<Label>("gold")->Text == "Gold: 25");
    CHECK(f.Find<Label>("message")->Text == "Found 25 gold coins!"); // a key and its count, formatted by the HUD

    f.Press(SDL_SCANCODE_E); // empty now
    f.Tick();
    CHECK(f.saves.Data().Get<i64>("gold", 0) == 25);
    CHECK(f.Find<Label>("message")->Text == "The chest is empty.");

    // The language changes: the HUD's gold follows (the event Application publishes).
    REQUIRE(Loc::SetLanguage("fr"));
    f.bus.PublishEngine(EngineEvents::LocaleChanged, { { "from", "en" }, { "to", "fr" } });
    f.Tick();
    CHECK(f.Find<Label>("gold")->Text == "Or\xC2\xA0: 25");
    CHECK(f.errors.empty());
}

TEST_CASE("The Scripted demo: the guard patrols and the Lua menu spends gold", "[script][demo]")
{
    Fixture f;
    const Entity guard = f.Demo().Ids.at("guard");
    const Vec2   start = f.Demo().GetWorld().GetTransform(guard)->Position;
    f.Tick(60);
    CHECK(f.Demo().GetWorld().GetTransform(guard)->Position.X > start.X + 50.0f);

    f.saves.Data().Set("gold", static_cast<i64>(12));
    f.Press(SDL_SCANCODE_L);
    f.Tick();
    REQUIRE(f.Find<Button>("buy"));
    CHECK(f.Find<Label>("gold")->Text == "You have 12 gold");
    f.Find<Button>("buy")->OnClick();
    CHECK(f.saves.Data().Get<i64>("gold", 0) == 7);
    CHECK(f.saves.Data().Get<i64>("lanterns", 0) == 1);

    f.console.Execute("gold 40"); // the scene script's console command
    CHECK(f.saves.Data().Get<i64>("gold", 0) == 40);
    CHECK(f.errors.empty());
}
