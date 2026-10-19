#version 450

// ---------------------------------------------------------------------------
// UI fragment shader — handles both regular textures and SDF text in one pass.
//
// mode == 0: sample texture × tint  (panels, images, progress bar fills)
// mode == 1: SDF distance field     (labels, button text via stb_truetype)
//
// Using a single pipeline for all UI lets the layer sort control draw order
// for both backgrounds and text without requiring separate render passes.
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D u_Texture;

layout(location = 0) in vec2  inUV;
layout(location = 1) in vec4  inTint;
layout(location = 2) in float inMode;
layout(location = 3) in vec3  inText;         // weight, outline, softness (distance-field units)
layout(location = 4) in vec4  inOutlineColor;

layout(location = 0) out vec4 outColor;

// SDF text: R encodes signed distance, the glyph's edge at 0.5 (128/255).
// The weight moves that edge out (faux bold). An outline is a second edge
// further out, filled with its own color under the glyph's; softness widens
// the outer edge's blur (a glow, a soft shadow).
vec4 sampleSDF(sampler2D tex)
{
    float dist      = texture(tex, inUV).r;
    float smoothing = max(fwidth(dist) * 0.5, 0.004);
    float edge      = 0.5 - inText.x;
    float fill      = smoothstep(edge - smoothing, edge + smoothing, dist);

    float outer      = edge - inText.y;
    float soft       = smoothing + inText.z;
    float coverage   = smoothstep(outer - soft, outer + smoothing, dist);
    vec4  background = inText.y > 0.0 ? inOutlineColor : vec4(inTint.rgb, inTint.a);
    vec4  color      = mix(background, inTint, fill);
    return vec4(color.rgb, color.a * max(coverage, fill));
}

void main()
{
    if (inMode > 0.5) {
        outColor = sampleSDF(u_Texture);
    } else {
        // Regular texture: sample and multiply by tint.
        // Solid-color quads bind a 1×1 white texture and set tint to the desired color.
        outColor = texture(u_Texture, inUV) * inTint;
    }
}
