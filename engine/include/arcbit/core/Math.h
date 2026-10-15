#pragma once

#include <arcbit/core/Types.h>

#include <cmath>

namespace Arcbit
{

// Common constants
constexpr f32 Pi = 3.14159265358979323846f;
constexpr f32 TwoPi = Pi * 2.0f;
constexpr f32 HalfPi = Pi * 0.5f;
constexpr f32 QuarterPi = Pi * 0.25f;
constexpr f32 Epsilon = 0.00001f;
constexpr f32 DegToRad = Pi / 180.0f;
constexpr f32 RadToDeg = 180.0f / Pi;


// ---------------------------------------------------------------------------
// Vec2 / Vec3 / Vec4
//
// Lightweight float vectors with named accessors and array indexing.
// Standard arithmetic operators (+, -, *, /) are included for game math;
// matrix operations and transforms are out of scope here and will live in
// the future math library (or potentially just glm).
//
// Named components: X, Y, Z, W
// Array access:     v[0] == X,  v[1] == Y,  etc.
// Raw pointer:      v.Data()  — for APIs that take float*.
// ---------------------------------------------------------------------------

struct Vec2
{
    f32 X = 0.0f, Y = 0.0f;

    constexpr Vec2() = default;

    constexpr Vec2(const f32 x, const f32 y) : X(x), Y(y) {}

    f32&                     operator[](const u32 i) { return (&X)[i]; }
    f32                      operator[](const u32 i) const { return (&X)[i]; }
    f32*                     Data() { return &X; }
    [[nodiscard]] const f32* Data() const { return &X; }

    constexpr Vec2 operator+(const Vec2& o) const { return { X + o.X, Y + o.Y }; }
    constexpr Vec2 operator-(const Vec2& o) const { return { X - o.X, Y - o.Y }; }
    constexpr Vec2 operator*(const f32 s) const { return { X * s, Y * s }; }
    constexpr Vec2 operator/(const f32 s) const { return { X / s, Y / s }; }

    constexpr Vec2& operator+=(const Vec2& o)
    {
        X += o.X;
        Y += o.Y;
        return *this;
    }

    constexpr Vec2& operator-=(const Vec2& o)
    {
        X -= o.X;
        Y -= o.Y;
        return *this;
    }

    constexpr Vec2& operator*=(const f32 s)
    {
        X *= s;
        Y *= s;
        return *this;
    }

    constexpr Vec2& operator/=(const f32 s)
    {
        X /= s;
        Y /= s;
        return *this;
    }

    constexpr bool operator==(const Vec2& o) const { return X == o.X && Y == o.Y; }
    constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }
};

[[nodiscard]] constexpr f32 Dot(const Vec2 a, const Vec2 b) noexcept { return a.X * b.X + a.Y * b.Y; }

// Prefer LengthSquared when only comparing magnitudes — it skips the sqrt.
[[nodiscard]] constexpr f32 LengthSquared(const Vec2 v) noexcept { return Dot(v, v); }
[[nodiscard]] inline f32    Length(const Vec2 v) noexcept { return std::sqrt(LengthSquared(v)); }

struct Vec3
{
    f32 X = 0.0f, Y = 0.0f, Z = 0.0f;

    constexpr Vec3() = default;

    constexpr Vec3(const f32 x, const f32 y, const f32 z) : X(x), Y(y), Z(z) {}

    f32&                     operator[](const u32 i) { return (&X)[i]; }
    f32                      operator[](const u32 i) const { return (&X)[i]; }
    f32*                     Data() { return &X; }
    [[nodiscard]] const f32* Data() const { return &X; }

    constexpr Vec3 operator+(const Vec3& o) const { return { X + o.X, Y + o.Y, Z + o.Z }; }
    constexpr Vec3 operator-(const Vec3& o) const { return { X - o.X, Y - o.Y, Z - o.Z }; }
    constexpr Vec3 operator*(const f32 s) const { return { X * s, Y * s, Z * s }; }
    constexpr Vec3 operator/(const f32 s) const { return { X / s, Y / s, Z / s }; }

    constexpr Vec3& operator+=(const Vec3& o)
    {
        X += o.X;
        Y += o.Y;
        Z += o.Z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& o)
    {
        X -= o.X;
        Y -= o.Y;
        Z -= o.Z;
        return *this;
    }

    constexpr Vec3& operator*=(const f32 s)
    {
        X *= s;
        Y *= s;
        Z *= s;
        return *this;
    }

    constexpr Vec3& operator/=(const f32 s)
    {
        X /= s;
        Y /= s;
        Z /= s;
        return *this;
    }

    constexpr bool operator==(const Vec3& o) const { return X == o.X && Y == o.Y && Z == o.Z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
};

struct Vec4
{
    f32 X = 0.0f, Y = 0.0f, Z = 0.0f, W = 1.0f;

    constexpr Vec4() = default;

    constexpr Vec4(const f32 x, const f32 y, const f32 z, const f32 w) : X(x), Y(y), Z(z), W(w) {}

    f32&                     operator[](const u32 i) { return (&X)[i]; }
    f32                      operator[](const u32 i) const { return (&X)[i]; }
    f32*                     Data() { return &X; }
    [[nodiscard]] const f32* Data() const { return &X; }

    constexpr Vec4 operator+(const Vec4& o) const { return { X + o.X, Y + o.Y, Z + o.Z, W + o.W }; }
    constexpr Vec4 operator-(const Vec4& o) const { return { X - o.X, Y - o.Y, Z - o.Z, W - o.W }; }
    constexpr Vec4 operator*(const f32 s) const { return { X * s, Y * s, Z * s, W * s }; }
    constexpr Vec4 operator/(const f32 s) const { return { X / s, Y / s, Z / s, W / s }; }

    constexpr Vec4& operator+=(const Vec4& o)
    {
        X += o.X;
        Y += o.Y;
        Z += o.Z;
        W += o.W;
        return *this;
    }

    constexpr Vec4& operator-=(const Vec4& o)
    {
        X -= o.X;
        Y -= o.Y;
        Z -= o.Z;
        W -= o.W;
        return *this;
    }

    constexpr Vec4& operator*=(const f32 s)
    {
        X *= s;
        Y *= s;
        Z *= s;
        W *= s;
        return *this;
    }

    constexpr Vec4& operator/=(const f32 s)
    {
        X /= s;
        Y /= s;
        Z /= s;
        W /= s;
        return *this;
    }

    constexpr bool operator==(const Vec4& o) const { return X == o.X && Y == o.Y && Z == o.Z && W == o.W; }
    constexpr bool operator!=(const Vec4& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Color
//
// RGBA float color in [0, 1]. Named accessors R/G/B/A and array indexing are
// both supported so it can be used both in game code and passed directly to
// Vulkan clear values (which expect float[4]).
//
// Common presets are exposed as static methods.
// ---------------------------------------------------------------------------
struct Color
{
    f32 R = 0.0f, G = 0.0f, B = 0.0f, A = 1.0f;

    constexpr Color() = default;

    constexpr Color(const f32 r, const f32 g, const f32 b, const f32 a = 1.0f) : R(r), G(g), B(b), A(a) {}

    f32&                     operator[](const u32 i) { return (&R)[i]; }
    f32                      operator[](const u32 i) const { return (&R)[i]; }
    f32*                     Data() { return &R; }
    [[nodiscard]] const f32* Data() const { return &R; }

    constexpr bool operator==(const Color& o) const { return R == o.R && G == o.G && B == o.B && A == o.A; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

    // -----------------------------------------------------------------------
    // Common presets
    // -----------------------------------------------------------------------
    static constexpr Color Transparent() { return { 0.0f, 0.0f, 0.0f, 0.0f }; }
    static constexpr Color Black() { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    static constexpr Color White() { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
    static constexpr Color Red() { return { 1.0f, 0.0f, 0.0f, 1.0f }; }
    static constexpr Color Green() { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
    static constexpr Color Blue() { return { 0.0f, 0.0f, 1.0f, 1.0f }; }
    static constexpr Color Yellow() { return { 1.0f, 1.0f, 0.0f, 1.0f }; }
    static constexpr Color Cyan() { return { 0.0f, 1.0f, 1.0f, 1.0f }; }
    static constexpr Color Magenta() { return { 1.0f, 0.0f, 1.0f, 1.0f }; }
    static constexpr Color CornflowerBlue() { return { 0.392f, 0.584f, 0.929f, 1.0f }; }
    static constexpr Color NaturalLight() { return { 1.0f, 0.8f, 0.4f, 1.0f }; }
    static constexpr Color WarmOrange() { return NaturalLight(); } // alias for natural light
    static constexpr Color NaturalDark() { return { 0.1f, 0.1f, 0.1f, 1.0f }; }
    static constexpr Color Gray() { return { 0.5f, 0.5f, 0.5f, 1.0f }; }
    static constexpr Color LightGray() { return { 0.75f, 0.75f, 0.75f, 1.0f }; }
    static constexpr Color DarkGray() { return { 0.25f, 0.25f, 0.25f, 1.0f }; }
    static constexpr Color Gold() { return { 1.0f, 0.84f, 0.0f, 1.0f }; }
    static constexpr Color WarmGold() {return {1.0f, 0.9f, 0.6f, 1.0f}; }
};
} // namespace Arcbit
