#include <arcbit/physics/ContactTracker.h>

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

} // namespace Arcbit
