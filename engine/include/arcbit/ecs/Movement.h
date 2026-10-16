#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
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

    // Tie-break state for tile movers, maintained by ResolveInputDirection.
    u8   HeldMask       = 0;     // MoveKeys held last tick, as bits
    bool PreferVertical = false; // axis that wins when both are held
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

// Turns held keys into a direction (+Y is down). Opposite keys cancel.
//   - cardinalOnly = false: diagonals allowed, normalized to unit length.
//   - cardinalOnly = true: one axis only; when both are held, the axis whose
//     key was pressed most recently wins (X if both went down together).
// Updates the tie-break state in `im`, so call it exactly once per tick.
[[nodiscard]] Vec2 ResolveInputDirection(InputMovement& im, MoveKeys keys, bool cardinalOnly);

// ---------------------------------------------------------------------------
// Style components — decide how the entity moves.
// ---------------------------------------------------------------------------

// Pixel-precise velocity integration with exponential friction decay.
// Use for combat, platformers, or any sub-tile positioning. With a
// MoveIntent, Velocity is set to Direction * Speed every tick (instant start
// and stop); without one, Velocity is yours to drive and Friction decays it.
struct FreeMovement
{
    Vec2 Velocity;
    f32  Friction = 8.0f;   // velocity multiplied by exp(-Friction * dt) each tick
    f32  MaxSpeed = 0.0f;   // clamp magnitude after integration; 0 = unlimited
    f32  Speed    = 100.0f; // world pixels/s at full MoveIntent
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
};

// Moves instantly to the adjacent tile. A fresh press steps at once; a held
// direction repeats every StepInterval. Driven only by MoveIntent.
struct SnapTileMovement
{
    f32 StepInterval = 0.15f; // seconds between steps while a direction is held
    f32 Cooldown     = 0.0f;  // time until the next step may start
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

} // namespace Arcbit
