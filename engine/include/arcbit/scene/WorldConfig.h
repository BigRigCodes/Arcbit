#pragma once

#include <arcbit/core/Math.h>

namespace Arcbit
{

// Scene-wide constants that systems read instead of hard-coding per-entity.
// Set once per scene in OnStart via GetScene().GetConfig().
struct WorldConfig
{
    f32  TileSize = 32.0f;          // world pixels per tile (used by tile movement systems)
    Vec2 Gravity  = { 0.0f, 0.0f }; // pixels/s² (zero = top-down, no gravity)

    // Tap to turn for tile movers without their own TurnDelay (seconds; 0 =
    // off). Application sets it on every scene from ProjectDefaults; scene
    // files don't store it.
    f32 TileTurnDelay = 0.0f;

    // Y-sort for sprites that don't say (SpriteRenderer::YSort). Set from
    // ProjectDefaults likewise; off for a scene made without an Application.
    bool YSortSprites = false;
};

} // namespace Arcbit
