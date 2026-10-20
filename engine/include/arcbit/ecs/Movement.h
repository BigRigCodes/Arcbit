#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/Entity.h>
#include <arcbit/input/InputTypes.h>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Movement intent — the shared path between controllers and styles.
// Controllers (InputMovement, AIMovement) write it; style systems read it.
// ---------------------------------------------------------------------------

// Which way the entity wants to move this tick. Game code reads this for
// facing / animation selection instead of polling input. A controller adds
// it on demand; add it yourself to drive a mover from a script or cutscene.
struct MoveIntent
{
    Vec2 Direction;               // zero = idle; length <= 1; cardinal for tile movers
    Vec2 Facing = { 0.0f, 1.0f }; // last nonzero Direction; starts facing down

    // Prefer this over writing Direction directly so Facing keeps up.
    void Set(const Vec2 dir)
    {
        Direction = dir;
        if (dir != Vec2{}) Facing = dir;
    }
};

// ---------------------------------------------------------------------------
// Controller components — decide where an entity wants to go.
// Pair with a style component to control how it gets there. Speed belongs to
// the style; a controller only picks the direction.
// ---------------------------------------------------------------------------

// Reads four InputManager actions each tick and writes MoveIntent.
struct InputMovement
{
    ActionID MoveLeft;
    ActionID MoveRight;
    ActionID MoveUp;
    ActionID MoveDown;

    // Most-recent-key state, maintained by ResolveInputDirection.
    u8   HeldMask       = 0;     // MoveKeys held last tick, as bits
    bool PreferVertical = false; // tile movers: axis that wins when both are held
    bool PreferLeft     = false; // which of Left / Right wins when both are held
    bool PreferUp       = false; // which of Up / Down wins when both are held
};

// Driven externally by the pathfinding system (Phase 30). Translates
// TargetPosition into MoveIntent the same way InputMovement does.
struct AIMovement
{
    Vec2 TargetPosition;
    bool HasTarget = false;
};

// Held state of the four move actions for one tick.
struct MoveKeys
{
    bool Left  = false;
    bool Right = false;
    bool Up    = false;
    bool Down  = false;
};

// Turns held keys into a direction (+Y is down). The most recently pressed
// key wins every conflict:
//   - Opposite keys (Left + Right): the one pressed last; releasing it hands
//     control back to the other.
//   - cardinalOnly = false: diagonals allowed, normalized to unit length.
//   - cardinalOnly = true: one axis only; the axis whose key was pressed last
//     wins (X if both went down together).
// Updates the tie-break state in `im`, so call it exactly once per tick.
[[nodiscard]] Vec2 ResolveInputDirection(InputMovement& im, MoveKeys keys, bool cardinalOnly);

// ---------------------------------------------------------------------------
// Style components — decide how the entity moves.
// ---------------------------------------------------------------------------

// Pixel-precise velocity integration with exponential friction decay.
// Use for combat, platformers, or any sub-tile positioning. With a
// MoveIntent, Velocity is set to Direction * Speed every tick (instant start
// and stop); without one, Velocity is yours to drive and Friction decays it.
//
// External is a push from the world (knockback, a bumper, a conveyor) that
// adds on top of Velocity and always decays with Friction — so it works on
// intent-driven movers, whose Velocity is rewritten every tick. A burst of
// speed v travels about v / Friction pixels; with Friction 0 it never stops.
struct FreeMovement
{
    Vec2 Velocity;
    f32  Friction = 8.0f;   // Velocity (without intent) and External scale by exp(-Friction * dt) each tick
    f32  MaxSpeed = 0.0f;   // clamp |Velocity| after integration; 0 = unlimited; External isn't clamped
    f32  Speed    = 100.0f; // world pixels/s at full MoveIntent
    Vec2 External;          // world push in px/s, added on top of Velocity

    // Add a burst of external velocity (px/s), e.g. knockback along a hit normal.
    void AddImpulse(const Vec2 velocity) { External = External + velocity; }
};

// Lerps from tile center to tile center. With a MoveIntent, QueuedDir /
// HasQueued are rewritten from it every tick; without one, queue directly.
struct SmoothTileMovement
{
    f32  Speed      = 4.0f;   // tiles/second; tile size comes from the TileMap
    Vec2 OriginWorld;
    Vec2 TargetWorld;
    f32  Progress   = 1.0f;   // 0→1 lerp progress; 1.0 = arrived
    Vec2 QueuedDir;
    bool HasQueued  = false;

    // Internal (managed by SmoothTileMoveSystem): the Pushable this step is
    // shoving, carried one tile ahead of the mover until the step ends.
    Entity _pushing = Entity::Invalid();
};

// Moves instantly to the adjacent tile. A fresh press steps at once; a held
// direction repeats every StepInterval. Driven only by MoveIntent.
struct SnapTileMovement
{
    f32 StepInterval = 0.15f; // seconds between steps while a direction is held
    f32 Cooldown     = 0.0f;  // time until the next step may start
};

// A mover walking into this entity shoves it instead of stopping (crates,
// barrels). The entity needs a Collider2D. Pushes stop at walls and don't
// chain through a second Pushable.
//   - Free movers shove it along, swept against the world, at
//     (1 - Resistance) of the speed they drive into it.
//   - Tile movers push it one tile, Sokoban-style, when the cell beyond is
//     free; a smooth mover carries it along in step. Keep tile-pushed crates
//     on the grid: don't also let free movers shove them.
struct Pushable
{
    f32 Resistance = 0.5f; // 0 = moves as fast as the pusher; 1 = immovable (tile pushes only check for 1)
};

// ---------------------------------------------------------------------------
// Resolver plumbing
// ---------------------------------------------------------------------------

// Tentative motion for the current tick. FreeMovementSystem writes a desired
// world-space delta here; CollisionResolutionSystem consumes it, clamps it
// via swept narrowphase, commits the final motion to Transform2D, and zeroes
// the field. FreeMovement entities **must** also have PendingMove —
// otherwise velocity decays from friction but the entity never moves.
struct PendingMove
{
    Vec2 DesiredDelta = {};
};

// ---------------------------------------------------------------------------
// Held - standing still because something has the entity's attention: a
// conversation holds the player and whoever they talk to. Each tick the
// hold system clears a held entity's MoveIntent direction after every
// controller and script has written it, so nothing walks it off (Facing is
// kept: it may still turn). Scripts that move things themselves can check
// entity:IsHeld(). Holds nest: each Hold needs its Release.
// ---------------------------------------------------------------------------
struct Held
{
    u32 Count = 0;
};

class World;
void               Hold(World& world, Entity entity);
void               Release(World& world, Entity entity); // the last release removes Held
[[nodiscard]] bool IsHeld(World& world, Entity entity);

} // namespace Arcbit
