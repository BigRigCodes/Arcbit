#pragma once

#include <arcbit/core/Hash.h>
#include <arcbit/core/Types.h>
#include <arcbit/events/EventPayload.h>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// EventId - an event's name hashed (HashName), so C++ publishes and
// subscribes with constants and no string work; Lua and the editor use the
// name. Lower-case dot paths: "game.coin.collected", "quest.started".
// "engine." is reserved for the engine's own events.
//
//   constexpr EventId CoinCollected = MakeEvent("game.coin.collected");
// ---------------------------------------------------------------------------
using EventId = u32;

constexpr EventId MakeEvent(const std::string_view name) { return HashName(name); }

struct Event
{
    EventId      Id = 0;
    EventPayload Payload;
};

using EventHandler = std::function<void(const Event&)>;

namespace Detail
{
    class EventSubscribers; // the bus's subscriber table (EventBus.cpp)
}

// ---------------------------------------------------------------------------
// Subscription - an owned subscription: destroying it (or Reset) stops the
// handler. Move-only. Keep it as long as the handler should run - a member
// of whatever the handler captures, so the two go together. Outliving the
// bus is harmless.
// ---------------------------------------------------------------------------
class Subscription
{
public:
    Subscription() = default;
    ~Subscription() { Reset(); }

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    Subscription(const Subscription&)            = delete;
    Subscription& operator=(const Subscription&) = delete;

    // Unsubscribe now. Safe during delivery: the handler isn't called again.
    void Reset();

    [[nodiscard]] bool IsActive() const;

private:
    friend class EventBus;
    Subscription(std::weak_ptr<Detail::EventSubscribers> table, u64 id) : _table(std::move(table)), _id(id) {}

    std::weak_ptr<Detail::EventSubscribers> _table;
    u64                                     _id = 0;
};

// ---------------------------------------------------------------------------
// EventBus - named publish / subscribe between systems that don't know each
// other (docs/events.md).
//
//   Subscription sub = events.Subscribe(CoinCollected, [this](const Event& e) {
//       _coins += e.Payload.Get<i64>("amount", 1);
//   });
//   events.Publish(CoinCollected, { { "amount", 5 } });
//
// Publish queues; Flush delivers the queue, in order. Application flushes
// once per fixed tick, after the scene update, so a handler never runs in
// the middle of the code that published - it can destroy entities or switch
// scenes safely. Events published during a flush are delivered in the same
// flush, up to MaxPerFlush (a guard against two handlers publishing to each
// other forever; the rest wait for the next flush). PublishNow delivers at
// once, for the rare case that needs it.
//
// Handlers run in subscription order. One subscribed during delivery starts
// with the next event; one unsubscribed during delivery isn't called again.
// Main thread only.
//
// Names: Register(name) records an event's name for logs, the monitor and
// Lua (Subscribe / Publish by name register it too). "engine." events are
// the engine's: game code may subscribe to them, but Publish of one is
// dropped with a warning, so they always mean what the docs say. The engine
// raises them with PublishEngine.
// ---------------------------------------------------------------------------
class EventBus
{
public:
    static constexpr std::string_view ReservedPrefix = "engine.";
    static constexpr size_t           MaxPerFlush    = 4096;

    EventBus();
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    // --- Names ---------------------------------------------------------------

    // Record an event's name and return its id. Logs an error if a different
    // name already hashes to the same id.
    EventId Register(std::string_view name);

    // The registered name; empty if the id was never registered.
    [[nodiscard]] std::string_view GetName(EventId id) const;

    // --- Subscribing ---------------------------------------------------------

    [[nodiscard]] Subscription Subscribe(EventId id, EventHandler handler);
    [[nodiscard]] Subscription Subscribe(std::string_view name, EventHandler handler);

    // --- Publishing ----------------------------------------------------------

    // Queue an event for the next Flush.
    void Publish(EventId id, EventPayload payload = {});
    void Publish(std::string_view name, EventPayload payload = {});

    // Deliver an event now, before returning.
    void PublishNow(EventId id, EventPayload payload = {});

    // Queue one of the engine's own ("engine.") events. Engine code only.
    void PublishEngine(EventId id, EventPayload payload = {});

    // Deliver everything queued (Application calls this once per tick). A
    // Flush called from inside a handler does nothing: the outer one carries on.
    void Flush();

    [[nodiscard]] size_t GetQueuedCount() const { return _queue.size(); }

    // --- Debugging -----------------------------------------------------------

    // Called with every event as it's delivered, subscribers or not - for the
    // editor's event monitor and debug overlays. Empty to clear.
    void SetMonitor(std::function<void(const Event&)> monitor) { _monitor = std::move(monitor); }

private:
    [[nodiscard]] bool IsReserved(EventId id) const;
    [[nodiscard]] bool AcceptsFromGame(EventId id) const; // logs when it doesn't
    void               Deliver(const Event& event);
    void               AssertMainThread() const;

    std::shared_ptr<Detail::EventSubscribers> _subscribers;
    std::unordered_map<EventId, std::string>  _names;
    std::deque<Event>                         _queue;
    std::function<void(const Event&)>         _monitor;
    bool                                      _flushing = false;
    std::thread::id                           _owner;
};
} // namespace Arcbit
