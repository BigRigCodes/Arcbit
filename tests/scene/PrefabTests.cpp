// Prefabs (docs/scenes.md#prefabs): a library of .arcprefab components with
// "extends", placed copies in scene files that keep a live link to their
// prefab and save only what they change, and one-time spawns.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/PrefabLibrary.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneFile.h>
#include <arcbit/scene/SceneRegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace Arcbit;
using nlohmann::json;

namespace
{
// Paths become fake handles and back; no device.
class FakeAssets final : public SceneAssets
{
public:
    TextureHandle LoadTexture(const std::string_view path) override
    {
        const auto [it, added] = _textures.try_emplace(std::string(path), TextureHandle::Make(10 + static_cast<u32>(_textures.size()), 1));
        return it->second;
    }
    std::string TexturePath(const TextureHandle texture) const override
    {
        for (const auto& [path, handle] : _textures)
            if (handle == texture) return path;
        return {};
    }
    SamplerHandle      Sampler(std::string_view) override { return SamplerHandle{ 1 }; }
    std::string        SamplerName(SamplerHandle) const override { return "nearest"; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(ActionID) const override { return {}; }
    bool               LoadTileMap(TileMap&, std::string_view) override { return false; }

private:
    std::map<std::string, TextureHandle> _textures;
};

PrefabLibrary Library()
{
    PrefabLibrary prefabs;
    prefabs.Add("prop", json::parse(R"({
        "SpriteRenderer": { "texture": "builtin:white", "tint": [1, 1, 1, 1] },
        "Collider2D": { "half_extents": [16, 16], "kind": "static" } })"));
    prefabs.Add("chest", json::parse(R"({ "extends": "prop",
        "Tag": { "value": "Chest" },
        "Transform2D": { "scale": [56, 56] },
        "SpriteRenderer": { "tint": [0.85, 0.6, 0.2, 1] },
        "Persistent": {} })"));
    return prefabs;
}

struct Fixture
{
    SceneRegistry          registry;
    FakeAssets             assets;
    PrefabLibrary          prefabs = Library();
    Scene                  scene;
    SceneFile::IdMap       ids;
    SceneFile::PrefabLinks links;

    Fixture() { RegisterBuiltinComponents(registry); }

    void Load(const json& entities)
    {
        REQUIRE(SceneFile::Load(scene, { { "version", 1 }, { "entities", entities } }, registry, assets, ids, &prefabs, &links));
    }
    json Saved() { return SceneFile::Save(scene, registry, assets, json::object(), ids, &links)["entities"]; }
    template<typename T>
    T* Get(const char* id) { return scene.GetWorld().GetComponent<T>(ids.at(id)); }
};
} // namespace

TEST_CASE("A prefab builds on the one it extends, field by field", "[scene][prefab]")
{
    PrefabLibrary prefabs = Library();
    const auto    chest   = prefabs.Resolve("chest");
    REQUIRE(chest);
    CHECK((*chest)["SpriteRenderer"]["texture"] == "builtin:white"); // from prop
    CHECK((*chest)["SpriteRenderer"]["tint"][0] == 0.85);              // its own
    CHECK((*chest)["Collider2D"]["kind"] == "static");
    CHECK_FALSE(chest->contains("extends"));

    prefabs.Add("ghost", json::parse(R"({ "extends": "chest", "Collider2D": null })"));
    CHECK_FALSE(prefabs.Resolve("ghost")->contains("Collider2D")); // null drops the parent's

    prefabs.Add("a", json::parse(R"({ "extends": "b" })"));
    prefabs.Add("b", json::parse(R"({ "extends": "a" })"));
    CHECK_FALSE(prefabs.Resolve("a"));       // a loop
    CHECK_FALSE(prefabs.Resolve("missing"));
    CHECK(prefabs.GetIds().front() == "a");
}

TEST_CASE("A placed copy is its prefab with its own fields over it", "[scene][prefab]")
{
    Fixture f;
    f.Load(json::parse(R"({
        "chest":   { "prefab": "chest", "Transform2D": { "position": [384, -192] } },
        "chest-2": { "prefab": "chest", "Transform2D": { "position": [64, 0] },
                     "SpriteRenderer": { "tint": [0.3, 0.3, 0.9, 1] }, "Collider2D": null } })"));

    CHECK(f.Get<Tag>("chest")->Value == "Chest");
    CHECK(f.Get<Transform2D>("chest")->Position == Vec2{ 384.0f, -192.0f });
    CHECK(f.Get<Transform2D>("chest")->Scale == Vec2{ 56.0f, 56.0f }); // the prefab's, kept
    CHECK(f.Get<SpriteRenderer>("chest")->Tint.R == 0.85f);
    CHECK(f.Get<SpriteRenderer>("chest-2")->Tint.B == 0.9f);            // its own
    CHECK(f.Get<Collider2D>("chest"));
    CHECK_FALSE(f.Get<Collider2D>("chest-2"));                           // removed
    CHECK(f.Get<Persistent>("chest")->Id == "chest");                    // its file id, as for any entity
}

TEST_CASE("A copy saves only what differs from its prefab", "[scene][prefab]")
{
    Fixture f;
    f.Load(json::parse(R"({
        "chest":   { "prefab": "chest", "Transform2D": { "position": [384, -192] } },
        "chest-2": { "prefab": "chest", "Transform2D": { "position": [64, 0] }, "Collider2D": null } })"));
    f.Get<SpriteRenderer>("chest")->FlipX = true; // a change made in the scene

    const json saved = f.Saved();
    CHECK(saved["chest"] == json::parse(R"({ "prefab": "chest", "Transform2D": { "position": [384.0, -192.0] },
                                             "SpriteRenderer": { "flip_x": true } })"));
    CHECK(saved["chest-2"] == json::parse(R"({ "prefab": "chest", "Transform2D": { "position": [64.0, 0.0] },
                                               "Collider2D": null })"));

    // The link is live: an edit to the prefab reaches every copy that didn't
    // change that field.
    Fixture g;
    g.prefabs.Add("prop", json::parse(R"({ "SpriteRenderer": { "texture": "builtin:white", "tint": [1, 1, 1, 1] },
                                           "Collider2D": { "half_extents": [20, 20], "kind": "static" } })"));
    g.Load(saved);
    CHECK(g.Get<Collider2D>("chest")->HalfExtents == Vec2{ 20.0f, 20.0f });
    CHECK(g.Get<SpriteRenderer>("chest")->FlipX);
    CHECK_FALSE(g.Get<Collider2D>("chest-2"));
}

TEST_CASE("Spawning a prefab stamps out an ordinary entity", "[scene][prefab]")
{
    Fixture      f;
    const Entity a = SpawnPrefab(f.scene, f.prefabs, "chest", { 10.0f, 20.0f }, f.registry, f.assets);
    const Entity b = SpawnPrefab(f.scene, f.prefabs, "chest", { 30.0f, 20.0f }, f.registry, f.assets);
    REQUIRE(a.IsValid());
    World& world = f.scene.GetWorld();
    CHECK(world.GetTransform(a)->Position == Vec2{ 10.0f, 20.0f });
    CHECK(world.GetComponent<Persistent>(a)->Id == "chest-1"); // unique, so each saves as its own spawn
    CHECK(world.GetComponent<Persistent>(b)->Id == "chest-2");
    CHECK(f.scene.GetPhysics().FindColliderForEntity(a) != PhysicsWorld::InvalidId);
    CHECK_FALSE(SpawnPrefab(f.scene, f.prefabs, "missing", {}, f.registry, f.assets).IsValid());
}
