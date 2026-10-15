#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/ecs/World.h>

#include <functional>

namespace Arcbit
{
// Overlap events for trigger colliders (Collider2D::IsTrigger). Attach to the
// trigger entity, the entity entering it, or both — every side that carries
// the component is called with itself as `self`.
//
// Callbacks run after the physics pass for the tick, so they may move,
// destroy, or add components freely. `other` may already be destroyed when
// OnExit fires (it expired or was destroyed while inside); check
// World::IsValid(other) before touching it.
struct TriggerCallback
{
    std::function<void(Entity self, Entity other)> OnEnter;
    std::function<void(Entity self, Entity other)> OnStay; // every tick while overlapping
    std::function<void(Entity self, Entity other)> OnExit;
};

// Blocking-contact events between two entity colliders. A contact starts when
// a mover is stopped by the other collider and lasts while the two stay within
// a 1 px skin of each other. Same both-sides and lifetime rules as
// TriggerCallback. There is deliberately no OnStay — see docs/physics.md.
struct CollisionCallback
{
    std::function<void(Entity self, Entity other, Vec2 normal)> OnEnter; // normal points from other toward self
    std::function<void(Entity self, Entity other)>              OnExit;
};
} // namespace Arcbit
