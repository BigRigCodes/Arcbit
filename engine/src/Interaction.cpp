#include <arcbit/ecs/Interaction.h>

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/ecs/World.h>

#include <cmath>
#include <limits>
#include <vector>

namespace Arcbit
{
namespace
{
    constexpr f32 DefaultReachTiles = 1.25f;
    constexpr f32 ConeCos           = 0.5f; // cos(60 degrees): "in front of"

    struct Candidate
    {
        Entity Id;
        Vec2   Position;
    };

    // Interactables other than the interactor itself, minus Disabled ones.
    std::vector<Candidate> Candidates(World& world, const Entity interactor)
    {
        std::vector<Candidate> found;
        world.Query<Interactable>().ForEach([&](const Entity e, const Interactable&) {
            if (e == interactor || world.GetComponent<Disabled>(e)) return;
            found.push_back({ e, world.GetTransform(e)->Position });
        });
        return found;
    }

    Vec2 SnapToTile(const Vec2 p, const f32 tile)
    {
        return { std::round(p.X / tile) * tile, std::round(p.Y / tile) * tile };
    }

    // The faced tile's neighbour step: the dominant axis of `facing`.
    Vec2 TileStep(const Vec2 facing)
    {
        if (std::abs(facing.X) >= std::abs(facing.Y)) return { facing.X < 0 ? -1.0f : 1.0f, 0.0f };
        return { 0.0f, facing.Y < 0 ? -1.0f : 1.0f };
    }

    Entity FacedTile(const std::vector<Candidate>& candidates, const Vec2 at, const Vec2 facing, const f32 tile)
    {
        const Vec2 target = SnapToTile(at, tile) + TileStep(facing) * tile;
        for (const Candidate& c : candidates)
            if (SnapToTile(c.Position, tile) == target) return c.Id;
        return Entity::Invalid();
    }

    Entity FacedNearest(const std::vector<Candidate>& candidates, const Vec2 at, const Vec2 facing, const f32 reach)
    {
        const f32 facingLength = Length(facing);
        const Vec2 forward     = facingLength > 0.0f ? facing / facingLength : Vec2{ 0.0f, 1.0f };
        Entity    inCone = Entity::Invalid(), any = Entity::Invalid();
        f32       coneBest = std::numeric_limits<f32>::max(), anyBest = coneBest;
        for (const Candidate& c : candidates) {
            const Vec2 to       = c.Position - at;
            const f32  distance = Length(to);
            if (distance > reach) continue;
            if (distance < anyBest) {
                anyBest = distance;
                any     = c.Id;
            }
            const bool ahead = distance == 0.0f || Dot(to / distance, forward) >= ConeCos;
            if (ahead && distance < coneBest) {
                coneBest = distance;
                inCone   = c.Id;
            }
        }
        return inCone.IsValid() ? inCone : any;
    }
} // namespace

Entity FindInteractTarget(World& world, const Entity interactor, const f32 tileSize)
{
    const Interactor* self = world.GetComponent<Interactor>(interactor);
    if (!self) return Entity::Invalid();
    const Vec2        at     = world.GetTransform(interactor)->Position;
    const MoveIntent* intent = world.GetComponent<MoveIntent>(interactor);
    const Vec2        facing = intent ? intent->Facing : Vec2{ 0.0f, 1.0f };

    const std::vector<Candidate> candidates = Candidates(world, interactor);
    const bool onTiles = world.GetComponent<SmoothTileMovement>(interactor) || world.GetComponent<SnapTileMovement>(interactor);
    if (onTiles) return FacedTile(candidates, at, facing, tileSize);
    const f32 reach = self->Reach > 0.0f ? self->Reach : DefaultReachTiles * tileSize;
    return FacedNearest(candidates, at, facing, reach);
}
} // namespace Arcbit
