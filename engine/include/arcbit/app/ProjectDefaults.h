#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// ProjectDefaults - the engine behavior a game can change in one place
// (ApplicationConfig::Defaults), until project.arcbit (Phase 38) reads the
// same fields from a file. Most can be changed per instance too; each field
// says where.
// ---------------------------------------------------------------------------
struct ProjectDefaults
{
    // Tap to turn: how long (seconds) a standing tile mover holds a new
    // direction before it walks - a quicker tap only turns it. 0 = off.
    // Per mover: SmoothTileMovement / SnapTileMovement::TurnDelay
    // ("turn_delay").
    f32 TileTurnDelay = 0.1f;
};
} // namespace Arcbit
