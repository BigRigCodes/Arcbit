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

    // One axis, most recent key wins. A key that went down this tick (and its
    // opposite didn't) becomes the preferred end; with both held, the
    // preferred end wins instead of the two cancelling.
    f32 ResolveAxis(const u8 held, const u8 pressed, const u8 negBit, const u8 posBit, bool& preferNeg)
    {
        const bool newNeg = (pressed & negBit) != 0;
        const bool newPos = (pressed & posBit) != 0;
        if (newNeg != newPos) preferNeg = newNeg;

        const bool neg = (held & negBit) != 0;
        const bool pos = (held & posBit) != 0;
        if (neg && pos) return preferNeg ? -1.0f : 1.0f;
        return (pos ? 1.0f : 0.0f) - (neg ? 1.0f : 0.0f);
    }

    // Same rule across axes, for tile movers: a key that went down this tick
    // makes its axis the preferred one. Keys on both axes going down together
    // prefer X.
    void UpdatePreferredAxis(InputMovement& im, const u8 pressed)
    {
        const bool newH = (pressed & HorizontalBits) != 0;
        const bool newV = (pressed & VerticalBits) != 0;
        if (newH || newV) im.PreferVertical = newV && !newH;
    }
} // namespace

Vec2 ResolveInputDirection(InputMovement& im, const MoveKeys keys, const bool cardinalOnly)
{
    const u8 held    = ToMask(keys);
    const u8 pressed = held & ~im.HeldMask;
    im.HeldMask      = held;
    UpdatePreferredAxis(im, pressed);

    Vec2 dir{ ResolveAxis(held, pressed, LeftBit, RightBit, im.PreferLeft),
              ResolveAxis(held, pressed, UpBit, DownBit, im.PreferUp) };
    if (dir.X == 0.0f || dir.Y == 0.0f) return dir;

    if (cardinalOnly) {
        if (im.PreferVertical) dir.X = 0.0f;
        else dir.Y = 0.0f;
        return dir;
    }
    return dir * (1.0f / Length(dir));
}
} // namespace Arcbit
