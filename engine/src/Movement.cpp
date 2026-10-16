#include <arcbit/ecs/Movement.h>

namespace Arcbit
{
namespace
{
    constexpr u8 LeftBit  = 1 << 0;
    constexpr u8 RightBit = 1 << 1;
    constexpr u8 UpBit    = 1 << 2;
    constexpr u8 DownBit  = 1 << 3;

    constexpr u8 HorizontalBits = LeftBit | RightBit;
    constexpr u8 VerticalBits   = UpBit | DownBit;

    u8 ToMask(const MoveKeys keys)
    {
        return static_cast<u8>((keys.Left ? LeftBit : 0) | (keys.Right ? RightBit : 0) |
                               (keys.Up ? UpBit : 0) | (keys.Down ? DownBit : 0));
    }

    // A key that went down this tick makes its axis the preferred one. Keys
    // on both axes going down together prefer X, matching the old demos.
    void UpdatePreferredAxis(InputMovement& im, const u8 held)
    {
        const u8 pressed = held & ~im.HeldMask;
        const bool newH  = (pressed & HorizontalBits) != 0;
        const bool newV  = (pressed & VerticalBits) != 0;
        if (newH || newV) im.PreferVertical = newV && !newH;
        im.HeldMask = held;
    }
} // namespace

Vec2 ResolveInputDirection(InputMovement& im, const MoveKeys keys, const bool cardinalOnly)
{
    UpdatePreferredAxis(im, ToMask(keys));

    Vec2 dir{ (keys.Right ? 1.0f : 0.0f) - (keys.Left ? 1.0f : 0.0f),
              (keys.Down ? 1.0f : 0.0f) - (keys.Up ? 1.0f : 0.0f) };
    if (dir.X == 0.0f || dir.Y == 0.0f) return dir;

    if (cardinalOnly) {
        if (im.PreferVertical) dir.X = 0.0f;
        else dir.Y = 0.0f;
        return dir;
    }
    return dir * (1.0f / Length(dir));
}
} // namespace Arcbit
