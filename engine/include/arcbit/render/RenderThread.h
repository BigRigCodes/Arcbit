#pragma once

#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/ui/UIRect.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// PointLight
//
// A dynamic point light submitted by the game thread each frame.
// The render thread uploads the full Lights list to a GPU SSBO each frame
// and passes lightCount via push constants. No fixed array limit.
// ---------------------------------------------------------------------------
struct PointLight
{
    // Center of the light in world-space pixels, matching Sprite::Position.
    Vec2 Position;

    // Radius in world-space pixels. The light falls off to zero at this distance.
    f32 Radius = 200.0f;

    // Multiplier applied to the light color. >1 is valid for HDR-style intensity.
    f32 Intensity = 1.0f;

    // RGBA light color. Alpha is not used for transparency — it is reserved
    // for future use (e.g. a falloff exponent).
    Color LightColor = Color::White();
};

// ---------------------------------------------------------------------------
// Shadow map constants
//
// ShadowResolution — angle buckets in each 1D shadow map (must match the
//   SHADOW_RESOLUTION define in sprite.frag).
// MaxShadowLights  — maximum shadow-casting lights per frame. Shadow-casting
//   lights beyond this count are rendered without shadows.
// ---------------------------------------------------------------------------
static constexpr u32 ShadowResolution = 256;
static constexpr u32 MaxShadowLights  = 8;

// Per-light shadow occlusion data computed by the game thread.
// Distances[i] = world-space distance from the light center to the nearest
// solid-tile occluder at angle (i / ShadowResolution) * 2π.  A value equal
// to the light's Radius means no occluder was found in that direction.
struct ShadowMapData
{
    u32 LightIndex;                  // index into FramePacket::Lights[]
    f32 Distances[ShadowResolution]; // one entry per angle bucket
};

// ---------------------------------------------------------------------------
// Sprite
//
// A world-space sprite submitted by the game thread each frame.
// The render thread sorts sprites by layer then groups them by texture to
// minimize descriptor switches, then issues one instanced draw call per group.
//
// Coordinate system
// -----------------
// Position and Size are in world-space pixels. The camera (CameraPosition on
// FramePacket) defines which world point maps to screen center. At zoom 1:
//   NDC = (worldPos - cameraPos) / (viewportSize * 0.5)
// Y+ is downward (screen-style), consistent with most 2D games.
// ---------------------------------------------------------------------------
struct Sprite
{
    TextureHandle Texture;       // albedo / sprite texture (set 0, required)
    TextureHandle NormalTexture; // normal map (set 1); leave invalid for flat-normal fallback
    SamplerHandle Sampler;       // filtering / addressing for albedo and normal

    Vec2   Position = {0.0f, 0.0f};             // world-space center in pixels
    Vec2   Size     = {64.0f, 64.0f};           // full width × height in pixels
    UVRect UV       = {0.0f, 0.0f, 1.0f, 1.0f}; // normalized sub-region; defaults to whole texture
    Color  Tint     = Color::White();           // per-sprite RGBA tint multiplied against the albedo
    f32    Rotation = 0.0f;                     // clockwise rotation in radians around the sprite center

    // Draw order. Sprites are rendered lower-layer-first (painter's algorithm).
    // Sprites on the same layer with the same texture are batched into one draw.
    i32 Layer = 0;

    // UI pipeline only: set true for SDF text quads emitted by Label/Button widgets.
    // False = regular texture × tint; true = SDF distance-field decode.
    bool SDFMode = false;

    // UI pipeline only: 0 = full-viewport scissor (no clipping).
    // N > 0 = index into FramePacket::UIClipRects (1-based). Set by ScrollPanel
    // on all quads it emits so the render thread applies the correct scissor rect.
    u16 ClipIndex = 0;
};

// ---------------------------------------------------------------------------
// DrawCall  (low-level escape hatch — prefer Sprite for game code)
//
// Directly specifies NDC-space position, scale, and UV, and requires the
// caller to own the pipeline. Rendered after all Sprites, in submission order.
// Useful for special effects or post-processing quads that don't fit the
// sprite batcher model.
// ---------------------------------------------------------------------------
struct DrawCall
{
    PipelineHandle Pipeline;      // pipeline to bind (must be a forward-compatible pipeline)
    TextureHandle  Texture;       // albedo texture (set 0)
    TextureHandle  NormalTexture; // normal map (set 1); leave invalid for flat-normal fallback
    SamplerHandle  Sampler;
    u32            VertexCount = 0;

    // NDC-space position and half-size.  (0,0) = screen center; (1,1) = bottom-right.
    Vec2   Position = {0.0f, 0.0f};
    Vec2   Scale    = {1.0f, 1.0f};
    UVRect UV       = {0.0f, 0.0f, 1.0f, 1.0f};
};

// ---------------------------------------------------------------------------
// FramePacket
//
// All state the render thread needs to produce one frame.
// Built each tick by the game thread and handed to RenderThread::SubmitFrame.
//
// Why a struct instead of direct device calls?
// -----------------------------------------------
// The game thread and render thread run concurrently. If the game thread
// called RenderDevice directly it would race with the render thread's own
// Vulkan calls. FramePacket is the handshake point: the game thread writes
// it, signals the render thread, then immediately starts building the *next*
// packet — both threads are busy at the same time with no shared mutable state.
// ---------------------------------------------------------------------------
struct FramePacket
{
    SwapchainHandle Swapchain;      // which swapchain to present to
    u32             Width      = 0; // current backbuffer dimensions in pixels
    u32             Height     = 0;
    Color           ClearColor = Color::Black(); // RGBA clear color for the color attachment

    // High-level sprite list. Sorted by layer then grouped by texture; the
    // render thread issues one instanced draw per texture group.
    std::vector<Sprite> Sprites;

    // World-space position that maps to the center of the screen.
    // Defaults to (0, 0) — a sprite at world (0, 0) renders at screen center.
    Vec2 CameraPosition = {0.0f, 0.0f};

    // Zoom multiplier. >1 zooms in (world pixels appear larger), <1 zooms out.
    // Applied by dividing the effective viewport size before the NDC transform.
    f32 CameraZoom = 1.0f;

    // Camera rotation in radians. The world rotates opposite to the camera angle.
    f32 CameraRotation = 0.0f;

    // Low-level draw calls rendered after all sprites, in submission order.
    // Requires the caller to own the pipeline and specify NDC coordinates.
    std::vector<DrawCall> DrawCalls;

    // Screen-space SDF text quads. Rendered with the SDF pipeline after all sprites.
    // Position and Size are in window pixels (0,0 = top-left). No camera transform or lighting.
    // Populated by DrawText() with a FontAtlas in SDF mode.
    std::vector<Sprite> SDFSprites;

    // UI widget quads — screen-space, no camera transform, no lighting.
    // Populated by UIManager::CollectRenderData each frame.
    // Rendered after sprites and legacy calls but before the SDF debug overlay.
    // Sprite::SDFMode controls rendering mode per quad (regular texture vs SDF text).
    std::vector<Sprite> UISprites;

    // Scissor clip rects for UISprites. Indexed by Sprite::ClipIndex (1-based; 0 = no clip).
    // Populated by ScrollPanel widgets during CollectRenderData. Each rect is in
    // screen pixels (top-left origin), matching the window coordinate system.
    std::vector<UIRect> UIClipRects;

    // Dynamic point light list. Uploaded to a per-frame SSBO each tick.
    // The forward+ fragment shader loops over all entries.
    std::vector<PointLight> Lights;

    // Shadow occlusion data for lights with CastsShadows=true, built by the game thread.
    // At most MaxShadowLights entries ordered by LightIndex. The render thread uploads
    // this to a fixed-size SSBO at set=3; the light SSBO encodes which row each
    // shadow-casting light uses (via the w component of its color field).
    std::vector<ShadowMapData> ShadowMaps;

    // Additive ambient light applied to every pixel regardless of light proximity.
    Color AmbientColor = Color{0.1f, 0.1f, 0.1f, 1.0f};

    // The game's logical design resolution in world pixels.
    // When set, the NDC transform always exposes exactly this many world pixels
    // regardless of the actual window size — resizing scales the rendered image
    // rather than revealing more of the world. Leave as {0, 0} to use the
    // actual window dimensions (shows more world on a larger window).
    Vec2 ReferenceSize = {};

    // Color used to fill the letterbox / pillarbox bars when the window aspect
    // ratio differs from ReferenceSize. Black by default.
    Color LetterboxColor = Color::Black();

    // Optional texture drawn in the bar regions (e.g. decorative artwork).
    // Leave invalid to use a solid LetterboxColor instead.
    TextureHandle LetterboxTexture;
    SamplerHandle LetterboxSampler;

    // If true, the render thread calls ResizeSwapchain(Width, Height) before
    // acquiring the next image. Set this when the window has been resized.
    // The flag is consumed once and does not persist across frames.
    bool NeedsResize = false;

    // Read this frame back as drawn before the UI - the world without menus,
    // HUD or fades - for a save thumbnail. The pixels arrive through
    // RenderThread::TakeCapture once the frame is done. Costs a GPU stall.
    bool CaptureWorld = false;

    // Read this frame back as finished, UI included - a screenshot. Arrives
    // the same way as CaptureWorld; ignored on a frame that asks for that.
    bool CaptureFrame = false;

    // Tilemap stats — written by the TilemapRender system during CollectRenderData.
    // Read by Application before SubmitFrame to populate _renderStats.
    u32 TilemapChunksTotal    = 0; // total chunks in the active tilemap
    u32 TilemapChunksRendered = 0; // chunks that passed the frustum cull
    u32 TilemapTilesRendered  = 0; // individual tile sprites submitted
};

// ---------------------------------------------------------------------------
// CapturedFrame - a frame read back for FramePacket::CaptureWorld or
// CaptureFrame: the game area (inside any letterbox bars), RGBA8, top row
// first, opaque.
// ---------------------------------------------------------------------------
struct CapturedFrame
{
    u32             Width  = 0;
    u32             Height = 0;
    std::vector<u8> Rgba;
};

// ---------------------------------------------------------------------------
// RenderStats
//
// Snapshot of per-frame rendering statistics written by the render thread
// after each frame and readable from any thread via RenderThread::GetStats().
// Values reflect the most recently completed frame.
// ---------------------------------------------------------------------------
struct RenderStats
{
    f32 FPS              = 0; // tracked by the application game loop
    u32 SpritesSubmitted = 0; // total sprites in packet.Sprites
    u32 SpriteBatches    = 0; // instanced GPU draw calls emitted by the batcher
    u32 LegacyDrawCalls  = 0; // GPU draw calls from packet.DrawCalls
    u32 ChunksTotal      = 0; // total chunks in the active tilemap
    u32 ChunksRendered   = 0; // chunks that passed the frustum cull this frame
    u32 TilesRendered    = 0; // tile sprites submitted to packet.Sprites
    u32 LightsActive     = 0; // point lights uploaded to the SSBO
};

// ---------------------------------------------------------------------------
// RenderThread
//
// Owns a dedicated OS thread that drives all per-frame GPU work.
//
// Threading model
// ---------------
//   Game thread  — builds FramePackets, calls SubmitFrame each tick.
//   Render thread — wakes on each SubmitFrame, runs Acquire→Record→Submit→
//                   Present, then sleeps waiting for the next packet.
//
// Back-pressure
// -------------
//   SubmitFrame blocks if the render thread hasn't finished the previous frame
//   yet. This ensures the game thread stays at most one frame ahead of the
//   GPU, which caps input latency and prevents memory usage from growing
//   without bound if the game logic ever runs faster than the renderer.
//
//   Diagram (time flows right):
//
//     Game  [build N]──submit──[build N+1]──wait──────submit──[build N+2]
//     Render           [render N]──────────────────[render N+1]
//                       ^                            ^
//                       game blocks here if render   game can go here as
//                       isn't done with N yet        soon as N is done
//
// Lifetime
// --------
//   1. Create resources (device, swapchain, pipelines, textures …)
//   2. renderThread.Start(device)
//   3. Main loop: call SubmitFrame each tick
//   4. renderThread.Stop()   ← blocks until all GPU work finishes
//   5. device->WaitIdle()    ← belt-and-suspenders
//   6. Destroy resources
// ---------------------------------------------------------------------------
class RenderThread
{
public:
    // Launch the render thread. device must outlive the RenderThread.
    // swapchainFormat is needed to create the sprite pipeline with the correct
    // color attachment format — pass RenderDevice::GetSwapchainColorFormat().
    void Start(RenderDevice* device, Format swapchainFormat);

    // Signal the thread to exit and block until it does.
    // After this returns, no Vulkan calls are in flight and the device can
    // safely be destroyed.
    void Stop();

    // Hand a completed FramePacket to the render thread for rendering.
    // Blocks briefly if the render thread is still finishing the previous frame.
    void SubmitFrame(FramePacket packet);

    // Block until the render thread has finished its current frame and the
    // GPU has drained. Until the next SubmitFrame nothing else touches the
    // device, so the main thread may create and destroy GPU resources
    // mid-run (scene switches, texture hot-reload). Outside that window,
    // Create* / Destroy* race the render thread. Costs a full GPU stall.
    void WaitIdle();

    // Returns a snapshot of the statistics from the most recently completed frame.
    // Safe to call from any thread — values are written with relaxed atomics.
    [[nodiscard]] RenderStats GetStats() const;

    // The frame a FramePacket::CaptureWorld asked for, once - empty until the
    // render thread has finished it. Safe to call from any thread.
    [[nodiscard]] std::optional<CapturedFrame> TakeCapture();

    // The 1×1 white texture and its sampler used by UIManager for solid-color quads.
    // Available after Start(); UIManager stores these during its own Init.
    [[nodiscard]] TextureHandle GetUIWhiteTexture() const { return _uiWhiteTex; }
    [[nodiscard]] SamplerHandle GetUIWhiteSampler() const { return _uiWhiteSampler; }

private:
    // Thread entry point — loops calling RenderFrame until Stop() is called.
    void Run();

    // Execute one frame: acquire image, record commands, submit, present.
    void RenderFrame(const FramePacket& packet);

    // ----- Start() helpers (called once on the render thread at startup) -----

    void CreateDefaultNormalResources();
    void CreateSpritePipeline(Format swapchainFormat);
    void CreateInstanceBuffers();
    void CreateLightSSBOs();
    void CreateShadowSSBOs();
    void CreateSDFPipeline(Format swapchainFormat);
    void CreateSDFInstanceBuffers();
    void CreateUIPipeline(Format swapchainFormat);
    void CreateUIResources();  // creates 1×1 white texture + sampler + instance buffers

    // ----- RenderFrame() helpers (called once per frame) ---------------------

    void UploadSDFInstances(const std::vector<const Sprite*>& sorted, u32 frameSlot);
    void DrawSDFBatches(CommandListHandle  cmd, const std::vector<const Sprite*>& sorted,
                        const FramePacket& packet, u32                            frameSlot) const;

    void UploadUIInstances(const std::vector<const Sprite*>& sorted, u32 frameSlot);
    void DrawUIBatches(CommandListHandle  cmd, const std::vector<const Sprite*>& sorted,
                       const FramePacket& packet, u32                            frameSlot) const;

    // Pixel-space rect of the rendered game area within the actual framebuffer.
    // When ReferenceSize is set and the window aspect differs from the design
    // aspect, this rect is inset to preserve the design ratio (letterbox / pillarbox).
    struct LetterboxRect
    {
        f32 X = 0, Y = 0, W = 0, H = 0;
    };

    // Computes the LetterboxRect for the current packet.
    [[nodiscard]] static LetterboxRect ComputeLetterbox(const FramePacket& packet);

    // Handles NeedsResize and AcquireNextImage with one automatic retry.
    // Returns an invalid handle if the frame should be skipped.
    [[nodiscard]] TextureHandle AcquireBackBuffer(const FramePacket& packet) const;

    // Grows the light SSBO if needed, uploads this frame's lights.
    // Encodes shadow row indices from packet.ShadowMaps into the light SSBO.
    // Returns the light count for use in push constants.
    [[nodiscard]] u32 UploadLights(const FramePacket& packet, u32 frameSlot);

    // Writes this frame's shadow map data to the fixed-size shadow SSBO.
    void UploadShadowData(const FramePacket& packet, u32 frameSlot);

    // Returns pointers into packet.Sprites sorted by (layer, texture, sampler).
    [[nodiscard]] static std::vector<const Sprite*> SortSprites(const std::vector<Sprite>& sprites);

    // UI variant: sorts by (layer, ClipIndex, texture, sampler, SDFMode) so scissor
    // rect changes can be applied per-batch without breaking Z order.
    [[nodiscard]] static std::vector<const Sprite*> SortUISprites(const std::vector<Sprite>& sprites);

    // Grows the instance buffer if needed, uploads sorted sprite data.
    void UploadInstances(const std::vector<const Sprite*>& sorted, u32 frameSlot);

    // Clears the full framebuffer to LetterboxColor, then clears the letterbox
    // rect interior to ClearColor. Draws the LetterboxTexture if one is set.
    void BeginRenderPass(CommandListHandle  cmd, TextureHandle           backBuffer,
                         const FramePacket& packet, const LetterboxRect& lb) const;

    // Binds the sprite pipeline, uploads push constants, and issues one
    // instanced draw call per texture group. Returns the batch count.
    [[nodiscard]] u32 DrawSpriteBatches(CommandListHandle                 cmd,
                                        const std::vector<const Sprite*>& sorted,
                                        u32                               frameSlot, const FramePacket&    packet,
                                        u32                               lightCount, const LetterboxRect& lb) const;

    // Issues legacy DrawCall entries in submission order, after all sprites.
    void DrawLegacyCalls(CommandListHandle cmd, const FramePacket& packet,
                         u32               frameSlot, u32          lightCount, const LetterboxRect& lb) const;

    // CaptureWorld: end the pass after the world, copy the backbuffer into a
    // readback buffer, and resume the pass (keeping what's drawn) for the UI.
    void SplitForCapture(CommandListHandle cmd, TextureHandle backBuffer, u32 width, u32 height);

    // Copy the backbuffer as it stands into a new readback buffer (the
    // rendering pass must be ended).
    void CopyForCapture(CommandListHandle cmd, u32 width, u32 height);

    // After the captured frame is presented: wait for the GPU, read the
    // pixels back, crop to the game area and hand them to TakeCapture.
    void FinishCapture(u32 width, u32 height, const LetterboxRect& lb);

private:
    RenderDevice* _device = nullptr;
    std::thread   _thread;

    // ----- Shared state — always accessed under _mutex --------------------

    std::mutex _mutex;

    // Render thread sleeps here waiting for the game thread to submit a frame.
    std::condition_variable _frameReady;

    // Game thread sleeps here when it is already one frame ahead of the render
    // thread (_slotFree == false). This is the back-pressure mechanism.
    std::condition_variable _frameDone;

    FramePacket _packet;           // pending frame written by SubmitFrame
    bool        _hasFrame = false; // true while _packet holds an unrendered frame
    bool        _slotFree = true;  // true once the render thread has consumed the packet
    bool        _running  = false; // set to false by Stop() to signal the thread to exit

    // ----- Frame capture (FramePacket::CaptureWorld) -------------------------

    Format                       _swapchainFormat = Format::Undefined; // BGRA swaps to RGBA on readback
    BufferHandle                 _readback;     // render thread only: alive from split to finish
    std::mutex                   _captureMutex; // guards _capture
    std::optional<CapturedFrame> _capture;

    // ----- Per-frame render statistics (atomic — written by render thread) --

    std::atomic<u32> _statSpritesSubmitted{0};
    std::atomic<u32> _statSpriteBatches{0};
    std::atomic<u32> _statDrawCalls{0};
    std::atomic<u32> _statLightsActive{0};

    // ----- Shared GPU resources — owned by the render thread ---------------

    static constexpr u32 FrameSlots = RenderDevice::MaxFramesInFlight;

    // Default 1×1 RGBA texture encoding a flat normal (0.5, 0.5, 1.0, 1.0).
    // Bound to set 1 when a Sprite or DrawCall provides no NormalTexture.
    TextureHandle _defaultNormalTex;
    SamplerHandle _defaultNormalSampler;

    // ----- Sprite batcher resources ----------------------------------------

    // Pipeline created from sprite.vert + sprite.frag in Start().
    // Per-instance vertex layout: 3 × vec4 (posSize, uv, tint) = 48 bytes/sprite.
    PipelineHandle _spritePipeline;

    // Per-frame instance buffers. The CPU writes sprite instances into
    // _instanceBuffers[currentFrame]; the other slot is still being read.
    std::array<BufferHandle, FrameSlots> _instanceBuffers = {};

    // Allocated capacity in each instance buffer (number of sprites).
    u32 _instanceBufferCapacity = 0;

    // ----- SDF text pipeline resources -------------------------------------

    // Pipeline created from sdf.vert + sdf.frag. Screen-space, no lighting.
    PipelineHandle _sdfPipeline;

    // Separate per-frame instance buffers for SDF text quads.
    std::array<BufferHandle, FrameSlots> _sdfInstanceBuffers        = {};
    u32                                  _sdfInstanceBufferCapacity = 0;

    // ----- UI pipeline resources -------------------------------------------

    // Pipeline created from ui.vert + ui.frag. Screen-space, handles both
    // regular texture quads (SDFMode=false) and SDF text (SDFMode=true).
    PipelineHandle _uiPipeline;

    std::array<BufferHandle, FrameSlots> _uiInstanceBuffers        = {};
    u32                                  _uiInstanceBufferCapacity = 0;

    // 1×1 white RGBA texture used for solid-color UI quads (panels, bars, etc.).
    TextureHandle _uiWhiteTex;
    SamplerHandle _uiWhiteSampler;

    // ----- Forward+ light SSBO resources -----------------------------------

    // One SSBO per frame-in-flight slot — same cycling rationale as instance buffers.
    std::array<BufferHandle, FrameSlots> _lightSSBO         = {};
    u32                                  _lightSSBOCapacity = 0;

    // ----- Shadow map SSBO resources ----------------------------------------

    // Fixed-size SSBO: MaxShadowLights rows × ShadowResolution float distances.
    // Cycled per frame like the light SSBO so the CPU can write while the GPU reads
    // the previous frame's data. Never resized — the capacity is fixed at allocation.
    std::array<BufferHandle, FrameSlots> _shadowSSBO = {};
};
} // namespace Arcbit
