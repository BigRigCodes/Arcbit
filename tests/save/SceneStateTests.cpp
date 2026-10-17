// Scene state: capture what the player changed against the authored scene,
// then apply it to a freshly built one - headless, with a small village of
// persistent and ordinary entities.

#include <arcbit/ecs/Components.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/save/SceneState.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneRegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

using namespace Arcbit;
using nlohmann::json;

namespace
{
struct Chest
{
    bool Open = false;
    i32  Gold = 10;
};

// No GPU: these tests only touch components that need no assets.
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

SceneRegistry MakeRegistry()
{
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    registry.RegisterComponent<Chest>(
        "Chest",
        [](Chest& c, const json& j, SceneAssets&) {
            if (j.contains("open")) c.Open = j["open"];
            if (j.contains("gold")) c.Gold = j["gold"];
        },
        [](const Chest& c, json& j, const SceneAssets&) {
            j["open"] = c.Open;
            j["gold"] = c.Gold;
        });
    return registry;
}

// The content: a chest, a boss, a lever that only keeps its position, and a
// rock that isn't persistent. `version` stands in for a content update.
struct Village
{
    Scene                         scene;
    std::map<std::string, Entity> e;

    explicit Village(const int version = 1, const bool chestOpen = false)
    {
        World& world = scene.GetWorld();
        const auto make = [&](const char* id, const Vec2 at) {
            const Entity entity                  = world.CreateEntity();
            world.GetTransform(entity)->Position = at;
            e[id]                                = entity;
        };
        make("chest", { version == 1 ? 100.0f : 140.0f, 0.0f }); // v2 moves the chest
        world.AddComponent<Persistent>(e["chest"], Persistent{ .Id = "chest" });
        world.AddComponent<Chest>(e["chest"], Chest{ .Open = chestOpen });

        make("boss", { 300, 0 });
        world.AddComponent<Persistent>(e["boss"], Persistent{ .Id = "boss" });

        make("lever", { 0, 50 });
        world.AddComponent<Persistent>(e["lever"], Persistent{ .Id = "lever", .Components = { "Transform2D" } });
        world.AddComponent<Chest>(e["lever"]); // not kept: the lever only keeps Transform2D

        make("rock", { 0, 0 });
        scene.GetTileMap().BeginTrackingChanges();
    }
    World& W() { return scene.GetWorld(); }
};

struct Fixture
{
    SceneRegistry registry = MakeRegistry();
    NoAssets      assets;

    // Play a village with `play`, capture what changed, and apply it to `fresh`.
    template<typename Play>
    SceneState Replay(Village& fresh, Play play)
    {
        Village             played;
        const AuthoredState authored = SceneStates::Snapshot(played.scene, registry, assets);
        play(played);
        const SceneState state = SceneStates::Capture(played.scene, authored, registry, assets);
        SceneStates::Apply(fresh.scene, state, registry, assets);
        return state;
    }
};
} // namespace

TEST_CASE("An untouched scene has nothing to save", "[save][scene]")
{
    Fixture             f;
    Village             village;
    const AuthoredState authored = SceneStates::Snapshot(village.scene, f.registry, f.assets);
    CHECK(SceneStates::Capture(village.scene, authored, f.registry, f.assets).Empty());
}

TEST_CASE("Changed fields come back; untouched entities stay as authored", "[save][scene]")
{
    Fixture          f;
    Village          fresh;
    const SceneState state = f.Replay(fresh, [](Village& v) {
        v.W().GetComponent<Chest>(v.e["chest"])->Open = true;
        v.W().GetTransform(v.e["rock"])->Position     = { 999, 999 }; // not persistent
    });

    CHECK(state.Entities == json{ { "chest", { { "Chest", { { "open", true } } } } } }); // only the changed field
    CHECK(fresh.W().GetComponent<Chest>(fresh.e["chest"])->Open);
    CHECK(fresh.W().GetComponent<Chest>(fresh.e["chest"])->Gold == 10);
    CHECK(fresh.W().GetTransform(fresh.e["rock"])->Position == Vec2{ 0, 0 }); // as authored
}

TEST_CASE("Destroyed, spawned, removed components and tiles all come back", "[save][scene]")
{
    Fixture          f;
    Village          fresh;
    const SceneState state = f.Replay(fresh, [](Village& v) {
        World& w = v.W();
        w.DestroyEntity(v.e["boss"]);           // beaten
        w.RemoveComponent<Chest>(v.e["chest"]); // a component removed

        const Entity drop              = w.CreateEntity(); // a dropped item
        w.GetTransform(drop)->Position = { 42, 7 };
        w.AddComponent<Persistent>(drop, Persistent{ .Id = "drop.1" });
        w.AddComponent<Chest>(drop, Chest{ .Open = true, .Gold = 3 });
        w.AddComponent<Collider2D>(drop, Collider2D{ .HalfExtents = { 4, 4 } });

        v.scene.GetTileMap().SetTile(2, 3, 1, 77); // a bridge built
    });

    CHECK(state.Destroyed == std::vector<std::string>{ "boss" });
    CHECK_FALSE(fresh.W().IsValid(fresh.e["boss"]));
    CHECK_FALSE(fresh.W().GetComponent<Chest>(fresh.e["chest"]));
    CHECK(fresh.scene.GetTileMap().GetTile(2, 3, 1) == 77);

    Entity drop = Entity::Invalid();
    fresh.W().Query<Persistent>().ForEach([&](const Entity e, const Persistent& p) {
        if (p.Id == "drop.1") drop = e;
    });
    REQUIRE(drop.IsValid());
    CHECK(fresh.W().GetTransform(drop)->Position == Vec2{ 42, 7 });
    CHECK(fresh.W().GetComponent<Chest>(drop)->Gold == 3);
    CHECK(fresh.scene.GetPhysics().FindColliderForEntity(drop) != PhysicsWorld::InvalidId); // in the broadphase

    // Applied tiles count as changes too, so the next capture keeps them.
    CHECK(fresh.scene.GetTileMap().GetChanges() == std::vector<TileMap::TileChange>{ { 2, 3, 1, 77 } });
}

TEST_CASE("A Persistent's component list limits what's kept", "[save][scene]")
{
    Fixture          f;
    Village          fresh;
    const SceneState state = f.Replay(fresh, [](Village& v) {
        v.W().GetTransform(v.e["lever"])->Position    = { 5, 50 };
        v.W().GetComponent<Chest>(v.e["lever"])->Open = true; // not in its list
    });
    CHECK(fresh.W().GetTransform(fresh.e["lever"])->Position == Vec2{ 5, 50 });
    CHECK_FALSE(fresh.W().GetComponent<Chest>(fresh.e["lever"])->Open);
}

TEST_CASE("Setting a field back to its default is a change too", "[save][scene]")
{
    // Diffing against authored, not against defaults: a chest authored open
    // and closed by the player must come back closed.
    Fixture             f;
    Village             played(1, true);
    const AuthoredState authored = SceneStates::Snapshot(played.scene, f.registry, f.assets);
    played.W().GetComponent<Chest>(played.e["chest"])->Open = false;
    const SceneState state = SceneStates::Capture(played.scene, authored, f.registry, f.assets);

    Village fresh(1, true);
    SceneStates::Apply(fresh.scene, state, f.registry, f.assets);
    CHECK_FALSE(fresh.W().GetComponent<Chest>(fresh.e["chest"])->Open);
}

TEST_CASE("A content update reaches fields the player never changed", "[save][scene]")
{
    Fixture             f;
    Village             played(1);
    const AuthoredState authored = SceneStates::Snapshot(played.scene, f.registry, f.assets);
    played.W().GetComponent<Chest>(played.e["chest"])->Open = true;
    const SceneState state = SceneStates::Capture(played.scene, authored, f.registry, f.assets);

    Village updated(2); // the chest moved in the new version
    SceneStates::Apply(updated.scene, state, f.registry, f.assets);
    CHECK(updated.W().GetTransform(updated.e["chest"])->Position == Vec2{ 140, 0 }); // the update shows
    CHECK(updated.W().GetComponent<Chest>(updated.e["chest"])->Open);                // the player's change kept
}

TEST_CASE("Scene and world state round-trip through JSON", "[save][scene]")
{
    SceneState state;
    state.Entities  = { { "chest", { { "Chest", { { "open", true } } }, { "Collider2D", nullptr } } } };
    state.Destroyed = { "boss" };
    state.Spawned   = { { "drop.1", { { "Tag", { { "value", "Drop" } } } } } };
    state.Tiles     = { { 2, -3, 1, 77 } };
    CHECK(SceneState::FromJson(state.ToJson()) == state);

    WorldState world;
    world.Scenes["Village"] = state;
    world.Scenes["Cave"]    = {}; // nothing changed there: not written
    WorldState back;
    back.FromJson(world.ToJson());
    CHECK(back.Scenes.size() == 1);
    CHECK(back.Scenes["Village"] == state);
}
