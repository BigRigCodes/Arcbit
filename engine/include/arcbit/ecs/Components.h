#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/render/RenderHandle.h>

#include <functional>
#include <string>
#include <string_view>

// Forward declarations — full types live in SpriteSheet.h.
namespace Arcbit { struct AnimationClip; class SpriteSheet; }

namespace Arcbit {

// ---------------------------------------------------------------------------
// Required components — every entity carries both.
// ---------------------------------------------------------------------------

// Single source of truth for where an entity is in the world.
// Scale is used as world-space pixel size by SpriteRenderSystem (e.g. {32, 32}).
struct Transform2D
{
    Vec2 Position = {};
    Vec2 Scale    = { 1.0f, 1.0f };
    f32  Rotation = 0.0f;             // radians, clockwise
};

// Coarse string classification. Use marker components for per-frame filtering;
// Tag is for setup queries and editor display.
struct Tag
{
    std::string Value = "Entity";
};

// ---------------------------------------------------------------------------
// Optional identity components
// ---------------------------------------------------------------------------

// Human-readable name. Add only for entities with game-design identity
// (player, named NPCs, quest objects). Not required — terrain and pooled
// enemies don't need names.
struct Name
{
    std::string Value;
};

// ---------------------------------------------------------------------------
// Render components
// ---------------------------------------------------------------------------

// Visual representation. SpriteRenderSystem reads (Transform2D, SpriteRenderer)
// and pushes a Sprite into FramePacket. Transform2D.Scale is the world-space size.
// FlipX mirrors the UV horizontally — used for left-facing sprites that share a
// right-facing animation row.
struct SpriteRenderer
{
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV    = { 0.0f, 0.0f, 1.0f, 1.0f };
    Vec2          Pivot = { 0.5f, 0.5f }; // normalised origin; AnimatorSystem writes this from frame data
    Color         Tint  = Color::White();
    i32           Layer = 0;
    bool          FlipX = false;
};

// Light properties. LightRenderSystem reads (Transform2D, LightEmitter)
// and pushes a PointLight into FramePacket.
struct LightEmitter
{
    f32   Radius       = 200.0f;
    f32   Intensity    = 1.0f;
    Color LightColor   = Color::NaturalLight();
    // When true, the light system raycasts solid tiles around this light each frame
    // and builds a 1D shadow map that the sprite shader uses for occlusion.
    // Tile IDs must have TileDef::BlocksLight=true to block light.
    bool  CastsShadows = false;
};

// ---------------------------------------------------------------------------
// Behavior components
// ---------------------------------------------------------------------------

// Scroll rate multiplier for parallax layers.
// SpriteRenderSystem applies: effectivePos = pos + camPos * (1 - scrollFactor).
//   0.0 = fixed (sky, overlay), 0.5 = mid-ground, 1.0 = normal world layer.
struct Parallax
{
    Vec2 ScrollFactor = { 0.5f, 0.5f };
};

// Marks the entity the scene camera should follow. If multiple entities carry
// this, CameraFollowSystem picks the first one it finds.
struct CameraTarget
{
    f32 Lag = 0.1f;  // 0.0 = instant snap, 1.0 = no follow
};

// Empty marker — all built-in systems use .Without<Disabled>() so disabled
// entities are invisible to them. Useful for pooling and off-screen NPCs.
struct Disabled {};

// Auto-destroys the entity after Remaining seconds reach zero.
// Intended for projectiles, particles, and temporary effects.
struct Lifetime
{
    f32 Remaining = 0.0f;
};

// ---------------------------------------------------------------------------
// Animation component
// ---------------------------------------------------------------------------

// Callback type for frame events. Set Animator::OnEvent to receive named events
// defined in the sprite JSON ("events": ["FootStep"]) as frames become active.
using AnimEventFn = std::function<void(std::string_view)>;

// Drives SpriteRenderer.UV from an AnimationClip each update tick.
// Set Clip and Sheet to a clip and its owning SpriteSheet; AnimatorSystem
// handles frame advancement, UV writes, and frame-event dispatch automatically.
// Both pointers must outlive the component — SpriteSheet assets are long-lived,
// so this is safe in normal usage.
struct Animator
{
    const AnimationClip* Clip       = nullptr;
    const SpriteSheet*   Sheet      = nullptr;
    u32                  FrameIndex = 0;
    f32                  Elapsed    = 0.0f;  // seconds into the current frame
    bool                 Playing    = true;
    bool                 Finished   = false; // true when a non-looping clip reaches its last frame
    AnimEventFn          OnEvent;            // optional; called for each event name on frame change
};

// ---------------------------------------------------------------------------
// Audio component
// ---------------------------------------------------------------------------

// Attaches a looping or one-shot sound to an entity with distance attenuation.
// AudioSystem initialises and spatialises the underlying sound automatically.
// Set Playing = false to stop and release the sound at runtime.
struct AudioSource
{
    std::string Path;              // path to audio file, relative to working directory
    f32         Volume  = 1.0f;   // [0, 1] local volume multiplier
    f32         Radius  = 500.0f; // max hearing distance in world units
    bool        Loop    = true;
    bool        Playing = true;
    // Internal handle managed by AudioSystem — do not set from game code.
    void*       _handle = nullptr;
};

} // namespace Arcbit
