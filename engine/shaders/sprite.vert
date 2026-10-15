#version 450

// ---------------------------------------------------------------------------
// Sprite batch vertex shader — per-instance world-space quads
//
// Each instance represents one sprite. The shader generates all 6 vertices
// of a two-triangle quad from gl_VertexIndex, reading the per-sprite data
// (world position, half-size, UV rect, tint) from the instance buffer.
//
// Coordinate system
// -----------------
// Position is in world pixels. The camera push constant defines which world
// point maps to screen center. Y+ is downward (screen convention).
// Camera rotation is pre-decomposed into (rotCos, rotSin) to avoid
// per-vertex trig. The view transform is:
//   delta  = worldPos - camPos
//   rotated.x = rotCos * delta.x + rotSin * delta.y
//   rotated.y = -rotSin * delta.x + rotCos * delta.y
//   NDC    = rotated / (viewportSize * 0.5)
// ---------------------------------------------------------------------------

// Per-instance attributes — one entry per sprite in the instance buffer.
// Packed as 4 × vec4 so Format::RGBA32_Float covers all four.
layout(location = 0) in vec4 a_PosSize; // (centerX, centerY, halfW, halfH) in world pixels
layout(location = 1) in vec4 a_UV;      // (u0, v0, u1, v1) normalized texture sub-region
layout(location = 2) in vec4 a_Tint;    // (r, g, b, a) linear tint color
layout(location = 3) in vec4 a_Rot;     // (rotCos, rotSin, _, _) sprite rotation

// Push constant layout must match SpritePushConstants on the CPU (44 bytes, tightly packed).
// ambient is split into four floats — see sprite.frag for the alignment rationale.
layout(push_constant) uniform PC {
    vec2  camPos;       // world-space position at screen center (pixels)
    vec2  viewportSize; // effective viewport in world units (window pixels / zoom)
    float rotCos;       // cos(cameraRotation) — precomputed on CPU
    float rotSin;       // sin(cameraRotation)
    float ambientR;
    float ambientG;
    float ambientB;
    float ambientA;
    uint  lightCount;   // active point lights in the SSBO
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec2 outWorldPos;  // world-space fragment position, forwarded for light distance math
layout(location = 2) out vec4 outTint;

void main()
{
    // Two-triangle quad — six vertices derived from gl_VertexIndex.
    // Corners are in local [-1, 1] space; we scale by half-size and offset
    // by the sprite center to get world-space positions.
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),   // top-left
        vec2( 1.0, -1.0),   // top-right
        vec2( 1.0,  1.0),   // bottom-right
        vec2(-1.0, -1.0),   // top-left  (second triangle)
        vec2( 1.0,  1.0),   // bottom-right
        vec2(-1.0,  1.0)    // bottom-left
    );

    vec2 corner = corners[gl_VertexIndex % 6];

    // Scale the unit corner to the sprite's extents first, then rotate around
    // the sprite center. The order matters for non-square sprites: rotating
    // the unit square and scaling afterward applies the scale along world
    // axes, which shears a rotated rectangle into a parallelogram.
    vec2 sc = corner * a_PosSize.zw;
    vec2 rc;
    rc.x = a_Rot.x * sc.x - a_Rot.y * sc.y;
    rc.y = a_Rot.y * sc.x + a_Rot.x * sc.y;
    vec2 worldPos = a_PosSize.xy + rc;

    // World → NDC with camera rotation. Rotate delta by -cameraAngle so the
    // world appears to rotate in the opposite direction to the camera.
    vec2  delta = worldPos - pc.camPos;
    vec2  rotated;
    rotated.x =  pc.rotCos * delta.x + pc.rotSin * delta.y;
    rotated.y = -pc.rotSin * delta.x + pc.rotCos * delta.y;
    vec2  ndc  = rotated / (pc.viewportSize * 0.5);

    // UV: bilinearly map the local corner to the atlas sub-region.
    // corner * 0.5 + 0.5 converts [-1,1] → [0,1]; mix selects between (u0,v0) and (u1,v1).
    outUV      = mix(a_UV.xy, a_UV.zw, corner * 0.5 + 0.5);
    outWorldPos = worldPos;
    outTint    = a_Tint;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
