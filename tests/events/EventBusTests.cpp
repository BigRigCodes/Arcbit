// EventBus: queued and immediate delivery, subscription lifetime, changes to
// the subscriber list during delivery, and the reserved engine names.

#include <arcbit/events/EngineEvents.h>
#include <arcbit/events/EventBus.h>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
constexpr EventId Coin = MakeEvent("game.coin.collected");
constexpr EventId Door = MakeEvent("game.door.opened");
constexpr EventId Ping = MakeEvent("test.ping");
constexpr EventId Pong = MakeEvent("test.pong");
} // namespace

TEST_CASE("Published events wait for Flush, then arrive with their payload", "[events]")
{
    EventBus     bus;
    i64          total = 0;
    Subscription sub   = bus.Subscribe(Coin, [&](const Event& e) { total += e.Payload.Get<i64>("amount", 0); });

    bus.Publish(Coin, { { "amount", 5 } });
    bus.Publish(Coin, { { "amount", 2 } });
    CHECK(total == 0); // queued, not delivered
    CHECK(bus.GetQueuedCount() == 2);

    bus.Flush();
    CHECK(total == 7);
    CHECK(bus.GetQueuedCount() == 0);
}

TEST_CASE("PublishNow delivers before returning", "[events]")
{
    EventBus     bus;
    int          calls = 0;
    Subscription sub   = bus.Subscribe(Coin, [&](const Event&) { ++calls; });
    bus.PublishNow(Coin);
    CHECK(calls == 1);
}

TEST_CASE("Handlers run in subscription order, only for their event", "[events]")
{
    EventBus                 bus;
    std::vector<std::string> log;
    Subscription             a = bus.Subscribe(Coin, [&](const Event&) { log.push_back("a"); });
    Subscription             b = bus.Subscribe(Coin, [&](const Event&) { log.push_back("b"); });
    Subscription             c = bus.Subscribe(Door, [&](const Event&) { log.push_back("door"); });
    bus.PublishNow(Coin);
    CHECK(log == std::vector<std::string>{ "a", "b" });
}

TEST_CASE("Destroying, resetting or moving a subscription controls the handler", "[events]")
{
    EventBus bus;
    int      calls = 0;
    {
        Subscription scoped = bus.Subscribe(Coin, [&](const Event&) { ++calls; });
        bus.PublishNow(Coin);
    } // destroyed: unsubscribed
    bus.PublishNow(Coin);
    CHECK(calls == 1);

    Subscription first = bus.Subscribe(Coin, [&](const Event&) { ++calls; });
    Subscription moved = std::move(first);
    CHECK_FALSE(first.IsActive());
    CHECK(moved.IsActive());
    bus.PublishNow(Coin);
    CHECK(calls == 2); // once, not twice

    moved.Reset();
    CHECK_FALSE(moved.IsActive());
    bus.PublishNow(Coin);
    CHECK(calls == 2);
}

TEST_CASE("A handler unsubscribed during delivery isn't called", "[events]")
{
    EventBus     bus;
    bool         laterCalled = false;
    Subscription later;
    Subscription first = bus.Subscribe(Coin, [&](const Event&) { later.Reset(); });
    later              = bus.Subscribe(Coin, [&](const Event&) { laterCalled = true; });
    bus.PublishNow(Coin);
    CHECK_FALSE(laterCalled);
}

TEST_CASE("A handler can unsubscribe itself while running", "[events]")
{
    EventBus     bus;
    int          calls = 0;
    Subscription self;
    self = bus.Subscribe(Coin, [&](const Event&) {
        ++calls;
        self.Reset(); // once only
    });
    bus.PublishNow(Coin);
    bus.PublishNow(Coin);
    CHECK(calls == 1);
}

TEST_CASE("A handler subscribed during delivery starts with the next event", "[events]")
{
    EventBus                  bus;
    int                       lateCalls = 0;
    std::vector<Subscription> added;
    Subscription              adder = bus.Subscribe(Coin, [&](const Event&) {
        // Many, so the handler list reallocates under the running delivery.
        for (int i = 0; i < 64; ++i) added.push_back(bus.Subscribe(Coin, [&](const Event&) { ++lateCalls; }));
    });
    bus.PublishNow(Coin);
    CHECK(lateCalls == 0);
    adder.Reset();
    bus.PublishNow(Coin);
    CHECK(lateCalls == 64);
}

TEST_CASE("Events published during a flush go out in the same flush, in order", "[events]")
{
    EventBus                 bus;
    std::vector<std::string> log;
    Subscription             coin = bus.Subscribe(Coin, [&](const Event&) {
        log.push_back("coin");
        bus.Publish(Door);
    });
    Subscription door = bus.Subscribe(Door, [&](const Event&) { log.push_back("door"); });
    bus.Publish(Coin);
    bus.Publish(Coin);
    bus.Flush();
    CHECK(log == std::vector<std::string>{ "coin", "coin", "door", "door" });
}

TEST_CASE("Handlers publishing to each other forever are cut off per flush", "[events]")
{
    EventBus     bus;
    size_t       delivered = 0;
    Subscription ping      = bus.Subscribe(Ping, [&](const Event&) { ++delivered; bus.Publish(Pong); });
    Subscription pong      = bus.Subscribe(Pong, [&](const Event&) { ++delivered; bus.Publish(Ping); });
    bus.Publish(Ping);
    bus.Flush(); // returns instead of looping
    CHECK(delivered == EventBus::MaxPerFlush);
    CHECK(bus.GetQueuedCount() == 1); // carried to the next flush
}

TEST_CASE("Game code can't publish engine events, but can hear them", "[events]")
{
    EventBus      bus;
    const EventId loaded = EngineEvents::SceneLoaded; // registered by every bus
    int           heard  = 0;
    Subscription  sub    = bus.Subscribe(loaded, [&](const Event&) { ++heard; });

    bus.Publish(loaded);
    bus.PublishNow(loaded);
    bus.Flush();
    CHECK(heard == 0); // dropped

    bus.PublishEngine(loaded);
    bus.Flush();
    CHECK(heard == 1);
}

TEST_CASE("Names register on use and read back", "[events]")
{
    EventBus     bus;
    int          heard = 0;
    Subscription sub   = bus.Subscribe("game.coin.collected", [&](const Event&) { ++heard; });
    CHECK(bus.GetName(Coin) == "game.coin.collected");
    CHECK(bus.GetName(Door).empty());

    bus.Publish("game.coin.collected"); // by name reaches the id's subscribers
    bus.Flush();
    CHECK(heard == 1);
    CHECK(bus.Register("game.door.opened") == Door);
}

TEST_CASE("SubscribeAll hears every event, before its own subscribers", "[events]")
{
    EventBus                 bus;
    std::vector<std::string> log;
    Subscription             coin = bus.Subscribe(Coin, [&](const Event&) { log.push_back("coin handler"); });
    Subscription             all  = bus.SubscribeAll([&](const Event& e) { log.push_back("all:" + bus.Describe(e.Id)); });
    bus.Publish(Coin);
    bus.PublishNow(Door); // no subscribers of its own: still heard
    bus.Flush();
    CHECK(log == std::vector<std::string>{ "all:#" + std::format("{:08x}", Door), "all:#" + std::format("{:08x}", Coin),
                                           "coin handler" });

    all.Reset();
    log.clear();
    bus.PublishNow(Door);
    CHECK(log.empty());
}

TEST_CASE("Describe names registered events and shows the id of others", "[events]")
{
    EventBus bus;
    bus.Register("game.door.opened");
    CHECK(bus.Describe(Door) == "game.door.opened");
    CHECK(bus.Describe(Coin) == std::format("#{:08x}", Coin));
}

TEST_CASE("A subscription outliving its bus is harmless", "[events]")
{
    Subscription survivor;
    {
        EventBus bus;
        survivor = bus.Subscribe(Coin, [](const Event&) {});
        CHECK(survivor.IsActive());
    }
    CHECK_FALSE(survivor.IsActive());
    survivor.Reset(); // no crash
}

TEST_CASE("Every engine event is named from the start", "[events]")
{
    const EventBus bus;
    for (const std::string_view name : EngineEvents::AllNames) CHECK(bus.GetName(MakeEvent(name)) == name);
}

TEST_CASE("A handler a SubscribeAll handler adds starts with the next event", "[events]")
{
    // Regression (PR #5 review): the event's own list was read after the
    // all-events handlers ran, so a handler they added heard this event too.
    EventBus     bus;
    int          lateCalls = 0;
    Subscription late;
    Subscription existing = bus.Subscribe(Coin, [](const Event&) {}); // the list exists already
    Subscription all      = bus.SubscribeAll([&](const Event&) {
        if (!late.IsActive()) late = bus.Subscribe(Coin, [&](const Event&) { ++lateCalls; });
    });
    bus.PublishNow(Coin);
    CHECK(lateCalls == 0);
    bus.PublishNow(Coin);
    CHECK(lateCalls == 1);
}
