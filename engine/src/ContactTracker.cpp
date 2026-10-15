#include <arcbit/physics/ContactTracker.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <algorithm>

namespace Arcbit
{
usize EntityPairHash::operator()(const EntityPair& p) const noexcept
{
    const auto pack = [](const Entity e) {
        return (static_cast<u64>(e.Generation) << 32) | static_cast<u64>(e.Index);
    };
    const std::hash<u64> h;
    // Order matters — (A, B) and (B, A) are different pairs.
    return h(pack(p.A)) ^ (h(pack(p.B)) * 0x9E3779B97F4A7C15ull);
}

void ContactTracker::Update(const std::vector<EntityPair>& current, Events& out)
{
    out.Entered.clear();
    out.Stayed.clear();
    out.Exited.clear();

    _next.clear();
    for (const EntityPair& p : current) {
        if (!_next.insert(p).second) continue; // duplicate report this tick
        (_active.contains(p) ? out.Stayed : out.Entered).push_back(p);
    }
    for (const EntityPair& p : _active)
        if (!_next.contains(p)) out.Exited.push_back(p);

    std::swap(_active, _next);
}

void ContactTracker::SelfTest()
{
#ifdef ARCBIT_DEBUG
    const Entity     a{ .Index = 1, .Generation = 0 };
    const Entity     b{ .Index = 2, .Generation = 0 };
    const Entity     c{ .Index = 3, .Generation = 0 };
    const EntityPair ab{ a, b };
    const EntityPair ac{ a, c };
    const EntityPair ba{ b, a };

    const auto has = [](const std::vector<EntityPair>& v, const EntityPair& p) {
        return std::ranges::find(v, p) != v.end();
    };

    ContactTracker         tracker;
    ContactTracker::Events ev;

    tracker.Update({ ab, ab }, ev);
    ARCBIT_ASSERT(ev.Entered.size() == 1 && has(ev.Entered, ab), "ContactTracker: first sighting must Enter once");

    tracker.Update({ ab, ac }, ev);
    ARCBIT_ASSERT(has(ev.Stayed, ab) && has(ev.Entered, ac) && ev.Exited.empty(),
                  "ContactTracker: repeat must Stay, new pair must Enter");

    tracker.Update({ ac }, ev);
    ARCBIT_ASSERT(has(ev.Exited, ab) && has(ev.Stayed, ac), "ContactTracker: absent pair must Exit");

    tracker.Update({ ab, ba }, ev);
    ARCBIT_ASSERT(has(ev.Entered, ab) && has(ev.Entered, ba) && has(ev.Exited, ac),
                  "ContactTracker: re-entry must Enter again; (A,B) and (B,A) are distinct");

    tracker.Update({}, ev);
    ARCBIT_ASSERT(ev.Exited.size() == 2 && tracker.Active().empty(), "ContactTracker: empty update must Exit all");

    LOG_DEBUG(Engine, "ContactTracker::SelfTest passed");
#endif
}
} // namespace Arcbit
