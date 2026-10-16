#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/ecs/World.h>

#include <unordered_set>
#include <vector>

namespace Arcbit
{
// Ordered pair of entities in contact. Callers decide the canonical order:
// (trigger, other) for trigger overlaps, lower entity index first for
// blocking contacts.
struct EntityPair
{
    Entity A = Entity::Invalid();
    Entity B = Entity::Invalid();

    bool operator==(const EntityPair&) const = default;
};

struct EntityPairHash
{
    [[nodiscard]] usize operator()(const EntityPair& p) const noexcept;
};

// Turns per-tick "these pairs are touching" snapshots into Enter / Stay / Exit
// events by diffing against the previous tick's set. Generic over what
// "touching" means — PhysicsWorld keeps one tracker for trigger overlaps and
// one for blocking contacts.
class ContactTracker
{
public:
    using PairSet = std::unordered_set<EntityPair, EntityPairHash>;

    struct Events
    {
        std::vector<EntityPair> Entered;
        std::vector<EntityPair> Stayed;
        std::vector<EntityPair> Exited;
    };

    // Replace the active set with `current` (duplicates are fine) and fill
    // `out` — cleared first — with the difference from the previous set.
    void Update(const std::vector<EntityPair>& current, Events& out);

    [[nodiscard]] const PairSet& Active() const noexcept { return _active; }
    [[nodiscard]] bool           IsActive(const EntityPair& p) const { return _active.contains(p); }
    void                         Clear() noexcept { _active.clear(); }

private:
    PairSet _active;
    PairSet _next; // scratch for Update, kept to reuse its buckets
};
} // namespace Arcbit
