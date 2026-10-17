#include <arcbit/events/EventBus.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <vector>

namespace Arcbit
{
namespace Detail
{
    // Handlers by event. A handler is held by shared_ptr so delivery can keep
    // the one it's calling alive while that handler subscribes (growing the
    // list) or unsubscribes itself. Removal during delivery only clears the
    // handler; the entry is compacted away once no delivery is running.
    class EventSubscribers
    {
    public:
        struct Entry
        {
            u64                           Id = 0;
            std::shared_ptr<EventHandler> Handler; // null once unsubscribed
        };

        std::unordered_map<EventId, std::vector<Entry>> ByEvent;
        std::unordered_map<u64, EventId>                EventOf; // subscription id -> its event
        u64                                             NextId     = 1;
        int                                             Delivering = 0; // nested deliveries running

        u64 Add(const EventId event, EventHandler handler)
        {
            const u64 id = NextId++;
            ByEvent[event].push_back({ id, std::make_shared<EventHandler>(std::move(handler)) });
            EventOf.emplace(id, event);
            return id;
        }

        void Remove(const u64 id)
        {
            const auto of = EventOf.find(id);
            if (of == EventOf.end()) return;
            if (const auto list = ByEvent.find(of->second); list != ByEvent.end())
                for (Entry& entry : list->second)
                    if (entry.Id == id) entry.Handler.reset();
            EventOf.erase(of);
            if (Delivering == 0) Compact();
        }

        [[nodiscard]] bool Contains(const u64 id) const { return EventOf.contains(id); }

        void Compact()
        {
            for (auto it = ByEvent.begin(); it != ByEvent.end();) {
                std::erase_if(it->second, [](const Entry& e) { return !e.Handler; });
                it = it->second.empty() ? ByEvent.erase(it) : std::next(it);
            }
        }
    };
} // namespace Detail

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------

Subscription::Subscription(Subscription&& other) noexcept :
    _table(std::move(other._table)), _id(std::exchange(other._id, 0))
{}

Subscription& Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other) {
        Reset();
        _table = std::move(other._table);
        _id    = std::exchange(other._id, 0);
    }
    return *this;
}

void Subscription::Reset()
{
    if (const auto table = _table.lock(); table && _id != 0) table->Remove(_id);
    _table.reset();
    _id = 0;
}

bool Subscription::IsActive() const
{
    const auto table = _table.lock();
    return table && table->Contains(_id);
}

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------

EventBus::EventBus() : _subscribers(std::make_shared<Detail::EventSubscribers>()), _owner(std::this_thread::get_id()) {}

EventBus::~EventBus() = default;

EventId EventBus::Register(const std::string_view name)
{
    const EventId id       = MakeEvent(name);
    const auto [it, added] = _names.try_emplace(id, name);
    if (!added && it->second != name)
        LOG_ERROR(Engine, "EventBus: events '{}' and '{}' hash to the same id - rename one", it->second, name);
    return id;
}

std::string_view EventBus::GetName(const EventId id) const
{
    const auto it = _names.find(id);
    return it != _names.end() ? std::string_view(it->second) : std::string_view{};
}

Subscription EventBus::Subscribe(const EventId id, EventHandler handler)
{
    AssertMainThread();
    return Subscription(_subscribers, _subscribers->Add(id, std::move(handler)));
}

Subscription EventBus::Subscribe(const std::string_view name, EventHandler handler)
{
    return Subscribe(Register(name), std::move(handler));
}

void EventBus::Publish(const EventId id, EventPayload payload)
{
    AssertMainThread();
    if (AcceptsFromGame(id)) _queue.push_back({ id, std::move(payload) });
}

void EventBus::Publish(const std::string_view name, EventPayload payload)
{
    Publish(Register(name), std::move(payload));
}

void EventBus::PublishNow(const EventId id, EventPayload payload)
{
    AssertMainThread();
    if (AcceptsFromGame(id)) Deliver({ id, std::move(payload) });
}

void EventBus::PublishEngine(const EventId id, EventPayload payload)
{
    AssertMainThread();
    _queue.push_back({ id, std::move(payload) });
}

void EventBus::Flush()
{
    AssertMainThread();
    if (_flushing) return;
    _flushing = true;

    // Take each event out before delivering it: its handlers may publish
    // more, which join the back of the queue and go out in this same flush.
    size_t delivered = 0;
    while (!_queue.empty() && delivered < MaxPerFlush) {
        const Event event = std::move(_queue.front());
        _queue.pop_front();
        Deliver(event);
        ++delivered;
    }
    if (!_queue.empty())
        LOG_WARN(Engine, "EventBus: {} events in one flush - handlers publishing to each other? {} wait for the next",
                 MaxPerFlush, _queue.size());
    _flushing = false;
}

bool EventBus::IsReserved(const EventId id) const
{
    return GetName(id).starts_with(ReservedPrefix);
}

bool EventBus::AcceptsFromGame(const EventId id) const
{
    if (!IsReserved(id)) return true;
    LOG_WARN(Engine, "EventBus: '{}' is an engine event - game code can't publish it", GetName(id));
    return false;
}

void EventBus::Deliver(const Event& event)
{
    if (_monitor) _monitor(event);

    Detail::EventSubscribers& table = *_subscribers;
    const auto                it    = table.ByEvent.find(event.Id);
    if (it == table.ByEvent.end()) return;

    // The list reference survives other events' lists being added (map
    // references are stable); index it fresh each time, since a handler
    // subscribing to this event can reallocate it. Handlers added now start
    // with the next event.
    std::vector<Detail::EventSubscribers::Entry>& list  = it->second;
    const size_t                                  count = list.size();
    ++table.Delivering;
    for (size_t i = 0; i < count; ++i) {
        const std::shared_ptr<EventHandler> handler = list[i].Handler;
        if (handler) (*handler)(event);
    }
    if (--table.Delivering == 0) table.Compact();
}

void EventBus::AssertMainThread() const
{
    ARCBIT_ASSERT(std::this_thread::get_id() == _owner, "EventBus: main thread only (not from OnPrepare)");
}
} // namespace Arcbit
