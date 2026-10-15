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
