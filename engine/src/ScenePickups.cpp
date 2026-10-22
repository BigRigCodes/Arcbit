// Items lying in the world (docs/items.md#pickups-and-drops): Scene::PickUp
// and the Items system that dresses pickups, takes touched ones and makes
// containers usable.

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Party.h>
#include <arcbit/items/Pickup.h>
#include <arcbit/scene/Scene.h>

#include <utility>
#include <vector>

namespace Arcbit
{
u32 Scene::PickUp(const Entity pickup, const Entity by)
{
    ItemPickup* p = _world.GetComponent<ItemPickup>(pickup);
    if (!p || !_items || !_items->Take) return 0;
    const std::string item   = p->Item;
    const u32         wanted = p->Count;
    const u32         taken  = wanted > 0 ? _items->Take(item, wanted, p->Bag) : 0;
    if (_events && taken > 0)
        _events->PublishEngine(EngineEvents::ItemPickedUp,
                               { { "item", item }, { "count", taken }, { "entity", pickup }, { "by", by } });
    if (taken == wanted) {
        _world.DestroyEntity(pickup);
        return taken;
    }
    p->Count -= taken;
    p->_armed = false; // waits for the player to step away and back
    if (_events)
        _events->PublishEngine(EngineEvents::ItemNotPickedUp, { { "item", item }, { "count", p->Count },
                                                                { "entity", pickup }, { "by", by }, { "reason", "no_room" } });
    return taken;
}

namespace
{
    struct Touch
    {
        Entity Pickup;
        Entity By;
    };

    // A pickup without a SpriteRenderer gets the host's look; an "interact"
    // one, and a Container, become Interactable so the interaction finder
    // sees them.
    void DressPickups(Scene& scene, const ItemHost& host)
    {
        World&              world = scene.GetWorld();
        std::vector<Entity> bare, unreachable;
        world.Query<ItemPickup>().ForEach([&](const Entity e, const ItemPickup& p) {
            if (!world.GetComponent<SpriteRenderer>(e)) bare.push_back(e);
            if (!p.OnTouch && !world.GetComponent<Interactable>(e)) unreachable.push_back(e);
        });
        world.Query<Container>().ForEach([&](const Entity e, const Container&) {
            if (!world.GetComponent<Interactable>(e)) unreachable.push_back(e);
        });
        for (const Entity e : unreachable) world.AddComponent<Interactable>(e);
        if (host.Dress)
            for (const Entity e : bare) host.Dress(scene, e);
    }

    // Touch pickups a party entity has just arrived at. A pickup with no
    // party entity in reach re-arms.
    std::vector<Touch> Arrivals(Scene& scene)
    {
        World& world = scene.GetWorld();
        std::vector<std::pair<Entity, Vec2>> party;
        world.Query<PartyLink, Transform2D>().Without<Disabled>().ForEach(
            [&](const Entity e, const PartyLink&, const Transform2D& t) { party.emplace_back(e, t.Position); });

        const f32          halfTile = scene.GetConfig().TileSize * 0.5f;
        std::vector<Touch> touched;
        world.Query<ItemPickup, Transform2D>().Without<Disabled>().ForEach(
            [&](const Entity e, ItemPickup& p, const Transform2D& t) {
                if (!p.OnTouch) return;
                const f32 reach = p.Reach > 0.0f ? p.Reach : halfTile;
                Entity    by    = Entity::Invalid();
                for (const auto& [member, at] : party)
                    if (Length(at - t.Position) <= reach) by = member;
                if (!by.IsValid()) p._armed = true;
                else if (p._armed) touched.push_back({ e, by });
            });
        return touched;
    }
} // namespace

void RegisterItemPickupSystem(World& world)
{
    world.RegisterSystem("Items", [](Scene& scene, f32) {
        const ItemHost* host = scene.GetItemHost();
        if (!host) return;
        DressPickups(scene, *host);
        for (const Touch& touch : Arrivals(scene))
            if (scene.GetWorld().IsValid(touch.Pickup)) (void)scene.PickUp(touch.Pickup, touch.By);
    });
}
} // namespace Arcbit
