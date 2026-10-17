#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/World.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <vector>

using namespace Arcbit;

namespace
{
// std::function with a small capture lives in its own small buffer on MSVC and
// points into itself, so a byte-wise relocation corrupts it. The shared_ptr
// counts live copies so destruction can be checked.
struct CallbackProbe
{
    std::function<u32()> Fn;
    std::shared_ptr<int> Token;
};
} // namespace

TEST_CASE("Entities start with Transform2D and Tag", "[ecs]")
{
    World        world;
    const Entity e = world.CreateEntity();
    CHECK(world.IsValid(e));
    CHECK(world.GetTransform(e) != nullptr);
    CHECK(world.GetComponent<Tag>(e) != nullptr);
}

TEST_CASE("Destroyed entities are rejected even after their slot is reused", "[ecs]")
{
    World        world;
    const Entity first = world.CreateEntity();
    world.DestroyEntity(first);
    CHECK_FALSE(world.IsValid(first));
    CHECK(world.GetTransform(first) == nullptr);

    const Entity second = world.CreateEntity();
    CHECK(second.Index == first.Index);           // slot recycled
    CHECK(second.Generation != first.Generation); // generation bumped
    CHECK(world.IsValid(second));
    CHECK_FALSE(world.IsValid(first));

    world.DestroyEntity(first); // stale handle: a no-op, must not hit the new entity
    CHECK(world.IsValid(second));
}

TEST_CASE("Add, get, and remove components keep other data intact", "[ecs]")
{
    World        world;
    const Entity e                  = world.CreateEntity();
    world.GetTransform(e)->Position = { 5.0f, 6.0f };

    world.AddComponent<Lifetime>(e, Lifetime{ .Remaining = 3.0f });
    REQUIRE(world.GetComponent<Lifetime>(e) != nullptr);
    CHECK(world.GetComponent<Lifetime>(e)->Remaining == 3.0f);
    CHECK(world.GetTransform(e)->Position == Vec2{ 5.0f, 6.0f }); // survived the archetype move

    world.RemoveComponent<Lifetime>(e);
    CHECK(world.GetComponent<Lifetime>(e) == nullptr);
    CHECK(world.GetTransform(e)->Position == Vec2{ 5.0f, 6.0f });
}

TEST_CASE("Queries match component sets and honor Without", "[ecs]")
{
    World        world;
    const Entity plain    = world.CreateEntity();
    const Entity timed    = world.CreateEntity();
    const Entity disabled = world.CreateEntity();
    world.AddComponent<Lifetime>(timed, Lifetime{ .Remaining = 1.0f });
    world.AddComponent<Lifetime>(disabled, Lifetime{ .Remaining = 2.0f });
    world.AddComponent<Disabled>(disabled);

    std::vector<Entity> seen;
    world.Query<Lifetime>().ForEach([&](const Entity e, Lifetime&) { seen.push_back(e); });
    CHECK(seen.size() == 2);

    seen.clear();
    world.Query<Lifetime>().Without<Disabled>().ForEach([&](const Entity e, Lifetime&) { seen.push_back(e); });
    CHECK(seen == std::vector{ timed });

    usize count = 0;
    world.Query<Transform2D>().ForEach([&](Transform2D&) { ++count; }); // component-only signature
    CHECK(count == 3);
    (void)plain;
}

TEST_CASE("Component storage relocates and destroys non-trivial components", "[ecs]")
{
    const auto token = std::make_shared<int>(0);
    {
        World               world;
        std::vector<Entity> entities;
        // Enough entities to force several column reallocations; each
        // AddComponent also moves the entity between archetypes.
        for (u32 i = 0; i < 40; ++i) {
            const Entity e = world.CreateEntity();
            world.AddComponent<CallbackProbe>(e, CallbackProbe{ [i] { return i; }, token });
            entities.push_back(e);
        }

        SECTION("relocated callbacks still work")
        {
            for (u32 i = 0; i < 40; ++i) CHECK(world.GetComponent<CallbackProbe>(entities[i])->Fn() == i);
        }

        SECTION("destroying entities releases their components and keeps the rest intact")
        {
            for (u32 i = 0; i < 40; i += 2) world.DestroyEntity(entities[i]); // swap-removes shuffle slots
            for (u32 i = 1; i < 40; i += 2) CHECK(world.GetComponent<CallbackProbe>(entities[i])->Fn() == i);
            CHECK(token.use_count() == 21);
        }
    }
    CHECK(token.use_count() == 1); // world teardown destroyed every component
}

TEST_CASE("OnRemove hooks run for RemoveComponent and DestroyEntity", "[ecs]")
{
    World            world;
    std::vector<f32> seen;
    world.OnRemove<Lifetime>([&](Entity, const Lifetime& lt) { seen.push_back(lt.Remaining); });

    const Entity a = world.CreateEntity();
    world.AddComponent<Lifetime>(a, Lifetime{ .Remaining = 1.0f });
    world.RemoveComponent<Lifetime>(a);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == 1.0f); // the hook sees the component before it goes

    const Entity b = world.CreateEntity();
    world.AddComponent<Lifetime>(b, Lifetime{ .Remaining = 2.0f });
    world.DestroyEntity(b);
    REQUIRE(seen.size() == 2);
    CHECK(seen[1] == 2.0f);

    world.DestroyEntity(a); // no Lifetime left: no call
    world.DestroyEntity(b); // already destroyed: no call
    CHECK(seen.size() == 2);
}

TEST_CASE("DestroyAllEntities runs OnRemove hooks for every live entity", "[ecs]")
{
    World world;
    int   removed = 0;
    world.OnRemove<Lifetime>([&](Entity, Lifetime&) { ++removed; });

    const Entity a = world.CreateEntity();
    const Entity b = world.CreateEntity();
    const Entity c = world.CreateEntity();
    world.AddComponent<Lifetime>(a);
    world.AddComponent<Lifetime>(b);
    world.DestroyEntity(b); // already gone: not counted twice

    world.DestroyAllEntities();
    CHECK(removed == 2);
    CHECK_FALSE(world.IsValid(a));
    CHECK_FALSE(world.IsValid(c));
}

TEST_CASE("OnAdd hooks run after AddComponent, with the added value", "[ecs]")
{
    World            world;
    std::vector<f32> seen;
    world.OnAdd<Lifetime>([&](Entity, const Lifetime& lt) { seen.push_back(lt.Remaining); });

    const Entity e = world.CreateEntity();
    world.AddComponent<Lifetime>(e, Lifetime{ .Remaining = 2.0f });
    CHECK(seen == std::vector<f32>{ 2.0f });
    world.RemoveComponent<Lifetime>(e);
    CHECK(seen.size() == 1); // removing isn't adding
}

TEST_CASE("OnDestroy hooks run for destroyed entities, not removed components", "[ecs]")
{
    World               world;
    std::vector<Entity> destroyed;
    world.OnDestroy([&](const Entity e) { destroyed.push_back(e); });

    const Entity a = world.CreateEntity();
    const Entity b = world.CreateEntity();
    world.AddComponent<Lifetime>(a, Lifetime{ .Remaining = 1.0f });
    world.RemoveComponent<Lifetime>(a);
    CHECK(destroyed.empty());

    world.DestroyEntity(a);
    world.DestroyAllEntities();
    CHECK(destroyed == std::vector<Entity>{ a, b });
}
