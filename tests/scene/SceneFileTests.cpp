// .arcscene format (docs/scenes.md), driven headless: a fake SceneAssets
// stands in for the GPU-backed one FileScene uses, so loading, saving and
// round trips are tested without a render device.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneFile.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/tilemap/TileMap.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>

using namespace Arcbit;
using nlohmann::json;

namespace
{
// Paths become fake handles (and back); the built-in white texture and the
// two stock samplers have fixed ones. Tilemaps load their chunks only - no
// atlases, since those need textures.
class FakeAssets final : public SceneAssets
{
public:
    TextureHandle LoadTexture(const std::string_view path) override
    {
        if (path == "builtin:white") return White;
        const auto [it, added] = _textures.try_emplace(std::string(path), TextureHandle::Make(10 + static_cast<u32>(_textures.size()), 1));
        return it->second;
    }
    std::string TexturePath(const TextureHandle texture) const override
    {
        if (texture == White) return "builtin:white";
        for (const auto& [path, handle] : _textures)
            if (handle == texture) return path;
        return {};
    }
    SamplerHandle Sampler(const std::string_view name) override { return name == "linear" ? Linear : Nearest; }
    std::string   SamplerName(const SamplerHandle sampler) const override
    {
        return sampler == Nearest ? "nearest" : sampler == Linear ? "linear" : "";
    }
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

    static constexpr TextureHandle White   = TextureHandle{ 1 };
    static constexpr SamplerHandle Nearest = SamplerHandle{ 1 };
    static constexpr SamplerHandle Linear  = SamplerHandle{ 2 };

private:
    std::map<std::string, TextureHandle> _textures;
};

// Stand-ins for the game-registered parts of the shipped demo scene.
struct DemoBumper
{
    f32 Kick = 600.0f;
};

SceneRegistry MakeRegistry(std::vector<std::string>* installed = nullptr)
{
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    registry.RegisterComponent<DemoBumper>(
        "DemoBumper", [](DemoBumper& c, const json& j, SceneAssets&) { c.Kick = j.value("kick", c.Kick); },
        [](const DemoBumper& c, json& j, const SceneAssets&) { j["kick"] = c.Kick; });
    for (const char* name : { "DemoBumpers", "DemoDebugKeys" })
        registry.RegisterSystem(name, [installed, name](Scene&) {
            if (installed) installed->emplace_back(name);
        });
    return registry;
}

json ShippedPhysicsScene()
{
    std::ifstream f(ArcbitTests::SourcePath("game/assets/scenes/physics.arcscene"));
    REQUIRE(f);
    return json::parse(f);
}

json WithoutEntities(json doc)
{
    doc.erase("entities");
    return doc;
}
} // namespace

TEST_CASE("The shipped physics scene loads its world and entities", "[scene][shipped]")
{
    std::vector<std::string> installed;
    const SceneRegistry      registry = MakeRegistry(&installed);
    FakeAssets               assets;
    Scene                    scene;
    SceneFile::IdMap         ids;
    REQUIRE(SceneFile::Load(scene, ShippedPhysicsScene(), registry, assets, ids));

    CHECK(scene.GetConfig().TileSize == 64.0f);
    CHECK(scene.GetCamera().Zoom == 1.5f);
    CHECK(installed == std::vector<std::string>{ "DemoBumpers", "DemoDebugKeys" });
    CHECK(scene.GetTileMap().GetTile(0, 0, 0) == 409); // the water square
    CHECK(scene.GetTileMap().GetTile(5, 0, 0) == 1);   // grass
    REQUIRE(scene.GetTileMap().FindTileDef(409) != nullptr);
    CHECK(scene.GetTileMap().FindTileDef(409)->Solid); // tile_overrides

    REQUIRE(ids.size() == 5);
    World&       world  = scene.GetWorld();
    const Entity player = ids.at("player");
    CHECK(world.GetComponent<Tag>(player)->Value == "Player");
    CHECK(world.GetComponent<Collider2D>(player)->Shape == ColliderShape::Circle);
    CHECK(world.GetComponent<InputMovement>(player)->MoveLeft == MakeAction("Move_Left"));
    CHECK(world.GetComponent<SpriteRenderer>(player)->Texture == FakeAssets::White);
    CHECK(world.GetComponent<FreeMovement>(player)->Speed == 240.0f);
    CHECK(world.GetComponent<FreeMovement>(player)->Friction == 8.0f); // default kept
    CHECK(world.GetComponent<Pushable>(ids.at("crate")) != nullptr);
    CHECK(world.GetComponent<DemoBumper>(ids.at("bumper"))->Kick == 700.0f);
    for (const Entity e : ids | std::views::values) // colliders registered after loading
        CHECK(scene.GetPhysics().FindColliderForEntity(e) != PhysicsWorld::InvalidId);
}

TEST_CASE("The shipped battle scene loads", "[scene][shipped]")
{
    std::ifstream f(ArcbitTests::SourcePath("game/assets/scenes/battle.arcscene"));
    REQUIRE(f);
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    Scene               scene;
    SceneFile::IdMap    ids;
    REQUIRE(SceneFile::Load(scene, json::parse(f), registry, assets, ids));
    CHECK(ids.size() == 8);
    CHECK(scene.GetWorld().GetComponent<InputMovement>(ids.at("player")) != nullptr);
}

TEST_CASE("Saving a loaded scene reproduces the file exactly", "[scene][shipped]")
{
    // The shipped file is kept in the saver's canonical form (sorted keys,
    // non-default fields only, tidy floats), so load -> save is the identity.
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    const json          file = ShippedPhysicsScene();

    Scene            scene;
    SceneFile::IdMap ids;
    REQUIRE(SceneFile::Load(scene, file, registry, assets, ids));
    const json saved = SceneFile::Save(scene, registry, assets, WithoutEntities(file), ids);
    INFO(saved.dump(2));
    CHECK(saved == file);
}

TEST_CASE("Only non-default fields are written", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    Scene               scene;
    World&              world = scene.GetWorld();

    const Entity e = world.CreateEntity();
    world.GetComponent<Tag>(e)->Value = "Rock";
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Speed = 50.0f });
    world.AddComponent<PendingMove>(e);

    const json entity = SceneFile::Save(scene, registry, assets, {}, {})["entities"]["rock"];
    CHECK(entity == json::parse(R"({ "FreeMovement": { "speed": 50.0 }, "PendingMove": {}, "Tag": { "value": "Rock" } })"));
}

TEST_CASE("Entities without file ids get ids from their tags", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    Scene               scene;
    for (int i = 0; i < 3; ++i) scene.GetWorld().GetComponent<Tag>(scene.GetWorld().CreateEntity())->Value = "Big Rock";

    const json entities = SceneFile::Save(scene, registry, assets, {}, {})["entities"];
    CHECK(entities.contains("big-rock"));
    CHECK(entities.contains("big-rock-2"));
    CHECK(entities.contains("big-rock-3"));
}

TEST_CASE("Collision layers read and write by name", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    const json          doc = json::parse(R"({ "version": 1, "entities": { "door": {
        "Collider2D": { "layer": "Trigger", "mask": ["Player", 4096], "is_trigger": true } } } })");

    Scene            scene;
    SceneFile::IdMap ids;
    REQUIRE(SceneFile::Load(scene, doc, registry, assets, ids));
    const Collider2D& col = *scene.GetWorld().GetComponent<Collider2D>(ids.at("door"));
    CHECK(col.Layer == CollisionLayers::Trigger);
    CHECK(col.Mask == (CollisionLayers::Player | 4096u));

    const json written = SceneFile::Save(scene, registry, assets, {}, ids)["entities"]["door"]["Collider2D"];
    CHECK(written["layer"] == "Trigger");
    CHECK(written["mask"] == json::parse(R"(["Player", 4096])"));
}

TEST_CASE("Load rejects what it can't build", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    Scene               scene;
    SceneFile::IdMap    ids;

    SECTION("a newer version") { CHECK_FALSE(SceneFile::Load(scene, json::parse(R"({ "version": 2 })"), registry, assets, ids)); }
    SECTION("no version") { CHECK_FALSE(SceneFile::Load(scene, json::parse(R"({ "entities": {} })"), registry, assets, ids)); }
    SECTION("an unregistered system")
    {
        CHECK_FALSE(SceneFile::Load(scene, json::parse(R"({ "version": 1, "systems": ["Nope"] })"), registry, assets, ids));
    }
    SECTION("a malformed component")
    {
        const json doc = json::parse(R"({ "version": 1, "entities": { "a": { "Transform2D": { "position": "here" } } } })");
        CHECK_FALSE(SceneFile::Load(scene, doc, registry, assets, ids));
    }
    SECTION("an unknown collision layer")
    {
        const json doc = json::parse(R"({ "version": 1, "entities": { "a": { "Collider2D": { "layer": "Dragons" } } } })");
        CHECK_FALSE(SceneFile::Load(scene, doc, registry, assets, ids));
    }
}

TEST_CASE("Unknown components are skipped, the rest of the entity loads", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    Scene               scene;
    SceneFile::IdMap    ids;
    const json          doc = json::parse(R"({ "version": 1, "entities": { "a": {
        "FromTheFuture": { "x": 1 }, "Lifetime": { "remaining": 2.0 } } } })");

    REQUIRE(SceneFile::Load(scene, doc, registry, assets, ids));
    CHECK(scene.GetWorld().GetComponent<Lifetime>(ids.at("a"))->Remaining == 2.0f);
}

TEST_CASE("A new entity never takes the id an existing one keeps", "[scene]")
{
    // Regression (PR #4 review): a new "Rock" reusing a lower slot was given
    // "rock" first, and the loaded "rock" then overwrote it on save.
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    const json          doc = json::parse(R"({ "version": 1, "entities": {
        "a":    { "Tag": { "value": "A" } },
        "rock": { "Tag": { "value": "Rock" } } } })");

    Scene            scene;
    SceneFile::IdMap ids;
    REQUIRE(SceneFile::Load(scene, doc, registry, assets, ids));
    World& world = scene.GetWorld();
    world.DestroyEntity(ids.at("a")); // frees the lower slot
    const Entity newer                    = world.CreateEntity();
    world.GetComponent<Tag>(newer)->Value = "Rock";
    REQUIRE(newer.Index < ids.at("rock").Index); // the new entity comes first when saving

    const json entities = SceneFile::Save(scene, registry, assets, {}, ids)["entities"];
    CHECK(entities.size() == 2);
    CHECK(entities.contains("rock"));
    CHECK(entities.contains("rock-2"));
}

TEST_CASE("A scene file that isn't an object, or has a string version, fails cleanly", "[scene]")
{
    const SceneRegistry registry = MakeRegistry();
    FakeAssets          assets;
    for (const char* text : { "[]", "null", R"({ "version": "1" })" }) {
        INFO(text);
        Scene            scene;
        SceneFile::IdMap ids;
        CHECK_FALSE(SceneFile::Load(scene, json::parse(text), registry, assets, ids)); // no throw
    }
}
