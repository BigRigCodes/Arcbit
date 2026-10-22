// Items lying in the world (docs/items.md#pickups-and-drops): party entities
// take pickups they walk onto or interact with, what doesn't fit stays, and
// ItemPickup in scene files.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/items/Party.h>
#include <arcbit/items/Pickup.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
constexpr f32      Dt       = 1.0f / 60.0f;
constexpr ActionID Interact = MakeAction("Interact");

struct Fixture
{
    ArcbitTests::TempDir    dir;
    ItemLibrary             library;
    std::unique_ptr<Inventories> bags;
    ItemHost                host;
    EventBus                events;
    InputManager            input;
    SceneManager            scenes;
    std::vector<Event>      heard;
    Subscription            all;
    Entity                  player;

    Fixture()
    {
        (void)dir.Write("a.arcitems", R"({ "potion": { "name": "Potion", "type": "consumable", "stack": 10 } })");
        library.LogIssues = false;
        REQUIRE(library.AddFolder(dir.File("")) == 1);
        bags = std::make_unique<Inventories>(library);
        InventoryRules oneSlot;
        oneSlot.Slots = 1;
        bags->Create("party", oneSlot);
        host.Take = [this](const std::string_view item, const u32 count, const std::string_view bag) {
            return bags->Find(bag)->Add(item, count);
        };
        input.RegisterAction(Interact, "Interact");
        input.BindKey(Interact, Key::Space);
        scenes.SetHost({ .Input = &input, .Events = &events, .Items = &host });
        scenes.SetActive(std::make_unique<Scene>());
        all    = events.SubscribeAll([this](const Event& e) { heard.push_back(e); });
        player = Ecs().CreateEntity();
        Ecs().AddComponent<PartyLink>(player);
        Ecs().AddComponent<MoveIntent>(player);
    }
    Scene&   GetScene() { return *scenes.GetActive(); }
    World&   Ecs() { return GetScene().GetWorld(); }
    Inventory& Bag() { return *bags->Find("party"); }
    void     MoveTo(const Vec2 at) { Ecs().GetTransform(player)->Position = at; }
    void     Tick()
    {
        GetScene().Update(Dt);
        events.Flush();
    }
    Entity Drop(const u32 count, const Vec2 at, const bool onTouch = true)
    {
        const Entity e                  = Ecs().CreateEntity();
        Ecs().GetTransform(e)->Position = at;
        Ecs().AddComponent<ItemPickup>(e, ItemPickup{ .Item = "potion", .Count = count, .OnTouch = onTouch });
        return e;
    }
    [[nodiscard]] std::vector<std::string> Names() const
    {
        std::vector<std::string> names;
        for (const Event& e : heard)
            if (e.Id == EngineEvents::ItemPickedUp || e.Id == EngineEvents::ItemNotPickedUp)
                names.push_back(std::string(e.Id == EngineEvents::ItemPickedUp ? "picked " : "left ") +
                                std::to_string(e.Payload.Get<i64>("count", 0)));
        return names;
    }
};
} // namespace

TEST_CASE("Walking onto a pickup takes what fits; the rest waits for the player to come back", "[items][pickup]")
{
    Fixture      f;
    const Entity potions = f.Drop(12, { 64.0f, 0.0f });
    f.Tick();
    CHECK(f.Bag().IsEmpty()); // two tiles away

    f.MoveTo({ 60.0f, 0.0f }); // within half a tile
    f.Tick();
    CHECK(f.Bag().Count("potion") == 10); // one slot, one stack of 10
    REQUIRE(f.Ecs().IsValid(potions));
    CHECK(f.Ecs().GetComponent<ItemPickup>(potions)->Count == 2);
    CHECK(f.Names() == std::vector<std::string>{ "picked 10", "left 2" });

    (void)f.Bag().Remove("potion", 10);
    f.Tick(); // still standing there: not again until they step off
    CHECK(f.Bag().IsEmpty());

    f.MoveTo({});
    f.Tick();
    f.MoveTo({ 64.0f, 0.0f });
    f.Tick();
    CHECK(f.Bag().Count("potion") == 2);
    CHECK_FALSE(f.Ecs().IsValid(potions));
    CHECK(f.Names().back() == "picked 2");
}

TEST_CASE("An interact pickup is taken by interacting, not by touch", "[items][pickup]")
{
    Fixture f;
    f.Ecs().AddComponent<Interactor>(f.player); // faces down
    const Entity potion = f.Drop(1, { 0.0f, 30.0f }, false);
    f.MoveTo({ 0.0f, 25.0f }); // right on it
    f.Tick();
    CHECK(f.Bag().IsEmpty());
    CHECK(f.Ecs().GetComponent<Interactable>(potion)); // so the interaction finder sees it

    f.MoveTo({});
    f.input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
    f.input.ProcessEdges();
    f.Tick();
    CHECK(f.Bag().Count("potion") == 1);
    CHECK_FALSE(f.Ecs().IsValid(potion));
}

TEST_CASE("Without a host, pickups lie where they are", "[items][pickup]")
{
    Scene        scene;
    const Entity potion = scene.GetWorld().CreateEntity();
    scene.GetWorld().AddComponent<ItemPickup>(potion, ItemPickup{ .Item = "potion" });
    const Entity player = scene.GetWorld().CreateEntity();
    scene.GetWorld().AddComponent<PartyLink>(player);
    scene.Update(Dt);
    CHECK(scene.GetWorld().IsValid(potion));
    CHECK(scene.PickUp(potion, player) == 0);
}

TEST_CASE("ItemPickup in a scene file", "[items][pickup][scene]")
{
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    const SceneRegistry::ComponentEntry* entry = registry.FindComponent("ItemPickup");
    REQUIRE(entry);
    Scene        scene;
    const Entity e = scene.GetWorld().CreateEntity();
    class : public SceneAssets
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
    } assets;
    entry->Read(scene.GetWorld(), e, nlohmann::json::parse(R"({ "item": "potion", "count": 3, "on": "interact" })"), assets);
    const ItemPickup& p = *scene.GetWorld().GetComponent<ItemPickup>(e);
    CHECK(p.Item == "potion");
    CHECK(p.Count == 3);
    CHECK_FALSE(p.OnTouch);
    CHECK(p.Bag == "party");

    const nlohmann::json written = entry->Write(scene.GetWorld(), e, assets);
    CHECK(written["on"] == "interact");
    CHECK(written["count"] == 3);
}
