#version 450

// ---------------------------------------------------------------------------
// UI vertex shader — screen-space quads for all UI widgets.
//
// Shares the SpriteInstance layout (4 × vec4) with sprite.vert and sdf.vert.
// a_Rot.xy is the sprite's own rotation as (cos, sin) - a spinner, the
// gamepad cursor's diamond; (1, 0) for everything unrotated. a_Rot.z carries
// a rendering mode flag set by the UI system:
//   0.0 = regular texture × tint  (panels, images, progress bars)
//   1.0 = SDF text rendering       (labels, button text)
// ---------------------------------------------------------------------------

layout(location = 0) in vec4 a_PosSize; // (centerX, centerY, halfW, halfH) screen pixels
layout(location = 1) in vec4 a_UV;      // (u0, v0, u1, v1)
layout(location = 2) in vec4 a_Tint;
layout(location = 3) in vec4 a_Rot;     // (rotCos, rotSin, mode, _)
layout(location = 4) in vec4 a_Text;    // (weight, outline, softness, shear) - text look, see ui.frag
layout(location = 5) in vec4 a_OutlineColor;

layout(push_constant) uniform PC {
    vec2 viewportSize;
} pc;

layout(location = 0) out vec2  outUV;
layout(location = 1) out vec4  outTint;
layout(location = 2) out float outMode;
layout(location = 3) out vec3  outText; // weight, outline, softness
layout(location = 4) out vec4  outOutlineColor;

void main()
{
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    vec2 corner = corners[gl_VertexIndex % 6];

    // Rotate the corner's offset from the quad's center, the same 2D rotation
    // as sprite.vert. Screen space runs y-down, so a positive angle turns
    // clockwise on screen, matching Sprite::Rotation. The UVs stay with the
    // corner, so the texture turns with the quad.
    vec2 offset    = corner * a_PosSize.zw;
    offset.x      -= a_Text.w * offset.y; // shear: the top leans right (faux italic; y runs down)
    vec2 rotated   = vec2(a_Rot.x * offset.x - a_Rot.y * offset.y,
                          a_Rot.y * offset.x + a_Rot.x * offset.y);
    vec2 screenPos = a_PosSize.xy + rotated;
    vec2 ndc       = (screenPos / pc.viewportSize) * 2.0 - 1.0;

    outUV   = mix(a_UV.xy, a_UV.zw, corner * 0.5 + 0.5);
    outTint = a_Tint;
    outMode = a_Rot.z;
    outText = a_Text.xyz;
    outOutlineColor = a_OutlineColor;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
