// EntityEvents: opt-in spawned / destroyed events for chosen entities,
// through a scene hosted by a SceneManager with an event bus (no GPU).

#include <arcbit/ecs/Components.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/scene/SceneManager.h>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
// Builds a player (opted in, named), a boss (opted in, renamed destroyed
// event) and a plain rock (not opted in).
class ArenaScene : public Scene
{
public:
    Entity Player, Boss, Rock;

    void OnLoad() override
    {
        World& world = GetWorld();
        Player       = world.CreateEntity();
        world.AddComponent<EntityEvents>(Player); // added before Name, as a scene file does
        world.AddComponent<Name>(Player, Name{ "Hero" });
        world.GetComponent<Tag>(Player)->Value = "Player";

        Boss = world.CreateEntity();
        world.AddComponent<EntityEvents>(Boss, EntityEvents{ .Spawned = false, .DestroyedEvent = "boss.defeated" });

        Rock = world.CreateEntity();
    }
};

struct Fixture
{
    EventBus                 events;
    std::vector<std::string> log;
    Subscription             all;
    SceneManager             scenes;

    Fixture()
    {
        scenes.SetHost({ .Events = &events });
        scenes.FadeOutTime = 0.0f;
        scenes.FadeInTime  = 0.0f;
        scenes.Register("Arena", [] { return std::make_unique<ArenaScene>(); });
        scenes.Register("Other", [] { return std::make_unique<Scene>(); });
        all = events.SubscribeAll([this](const Event& e) {
            const std::string name = std::string(events.GetName(e.Id));
            if (name.starts_with("engine.scene.")) return; // not what these tests are about
            log.push_back(std::format("{}:{}", name, e.Payload.Get<std::string>("name", "")));
        });
    }

    void Go(const char* name)
    {
        scenes.Switch(name);
        for (int i = 0; i < 10; ++i) scenes.Update(1.0f / 60.0f);
        events.Flush();
    }
    // One scene tick and a flush, the way Application runs them.
    void Tick()
    {
        scenes.GetActive()->Update(1.0f / 60.0f);
        events.Flush();
    }
    ArenaScene& Arena() { return *static_cast<ArenaScene*>(scenes.GetActive()); }
};
} // namespace

TEST_CASE("Opted-in entities announce their spawn on the next tick, name and all", "[events][entity]")
{
    Fixture f;
    f.Go("Arena");
    CHECK(f.log.empty()); // not yet: the spawn waits for the scene's next update
    f.Tick();
    CHECK(f.log == std::vector<std::string>{ "engine.entity.spawned:Hero" }); // not the boss (off), not the rock
}

TEST_CASE("Destroying an opted-in entity announces it, under its own name if renamed", "[events][entity]")
{
    Fixture f;
    f.Go("Arena");
    f.Tick();
    f.log.clear();

    World& world = f.Arena().GetWorld();
    world.DestroyEntity(f.Arena().Rock);
    world.DestroyEntity(f.Arena().Boss);
    world.DestroyEntity(f.Arena().Player);
    f.events.Flush();
    CHECK(f.log == std::vector<std::string>{ "boss.defeated:", "engine.entity.destroyed:Hero" });
}

TEST_CASE("Removing the component or unloading the scene isn't a death", "[events][entity]")
{
    Fixture f;
    f.Go("Arena");
    f.Tick();
    f.log.clear();

    f.Arena().GetWorld().RemoveComponent<EntityEvents>(f.Arena().Boss);
    f.Go("Other"); // the arena and its player go with the unload
    CHECK(f.log.empty());
}

TEST_CASE("An entity that comes and goes within a tick never announces its spawn", "[events][entity]")
{
    Fixture f;
    f.Go("Arena");
    World&       world = f.Arena().GetWorld();
    const Entity blip  = world.CreateEntity();
    world.AddComponent<EntityEvents>(blip, EntityEvents{ .Destroyed = false });
    world.DestroyEntity(blip);
    f.Tick();
    CHECK(f.log == std::vector<std::string>{ "engine.entity.spawned:Hero" }); // just the player
}
