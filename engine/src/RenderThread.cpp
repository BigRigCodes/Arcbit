#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <vector>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// GPU-side structs
// ---------------------------------------------------------------------------

// Point light layout — must match std430 in sprite.frag / forward.frag:
//   struct PointLight { vec2 position; float radius; float intensity; vec4 color; };
// vec4.w (ColorA in older code, ShadowRow here) encodes the shadow SSBO row:
//   -1.0 = this light casts no shadows; N.0 = row N in the shadow SSBO.
// 32 bytes, naturally aligned.
struct GpuPointLight
{
    f32 PosX, PosY;
    f32 Radius;
    f32 Intensity;
    f32 ColorR, ColorG, ColorB;
    f32 ShadowRow; // -1.0 = no shadow, 0..MaxShadowLights-1 = shadow SSBO row
};

// Per-sprite instance data uploaded to the instance vertex buffer.
// Packed as 4 × vec4 so all attributes use Format::RGBA32_Float.
// Must match the layout(location = 0/1/2/3) inputs in sprite.vert.
struct SpriteInstance
{
    f32 PosX, PosY, HalfW, HalfH;   // world-space center + half-extents (location 0)
    f32 U0, V0, U1, V1;             // normalized UV rect                (location 1)
    f32 TintR, TintG, TintB, TintA; // RGBA tint                         (location 2)
    f32 RotCos, RotSin, _pad0, _pad1; // sprite rotation cos/sin          (location 3)
}; // 64 bytes

// Push constants for the sprite pipeline.
// Same block declared in both sprite.vert and sprite.frag; each stage
// only reads the fields relevant to it.
//   Vertex:   camPos, viewportSize
//   Fragment: ambient, lightCount
struct SpritePushConstants
{
    f32 CamPosX, CamPosY;
    f32 ViewportW, ViewportH;
    f32 RotCos, RotSin;         // precomputed cos/sin of camera rotation
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
}; // 44 bytes

// Push constants for the SDF text pipeline (sdf.vert).
// Screen-space only — just the viewport size to map pixels to NDC.
struct SDFPushConstants
{
    f32 ViewportW, ViewportH;
}; // 8 bytes

// Push constants for the legacy DrawCall (forward) pipeline.
// Must match ForwardPushConstants layout in forward.vert / forward.frag.
struct ForwardPushConstants
{
    f32 PositionX, PositionY;
    f32 ScaleX, ScaleY;
    f32 U0, V0, U1, V1;
    f32 AmbientR, AmbientG, AmbientB, AmbientA;
    u32 LightCount;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

std::vector<u8> LoadShaderFile(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ARCBIT_ASSERT(file.is_open(), "RenderThread: failed to open shader file - check working directory");
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> buf(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Start() helpers
// ---------------------------------------------------------------------------

void RenderThread::CreateDefaultNormalResources()
{
    // (128, 128, 255, 255) → normalized (0.502, 0.502, 1.0) in the shader,
    // which decodes to a normal pointing straight out of the screen (+Z tangent).
    TextureDesc normalDesc{};
    normalDesc.Width     = 1;
    normalDesc.Height    = 1;
    normalDesc.Format    = Format::RGBA8_UNorm;
    normalDesc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    normalDesc.DebugName = "default_normal";
    _defaultNormalTex   = _device->CreateTexture(normalDesc);

    const u8 flatNormal[4] = { 128, 128, 255, 255 };
    _device->UploadTexture(_defaultNormalTex, flatNormal, sizeof(flatNormal));

    SamplerDesc sampDesc{};
    sampDesc.MinFilter     = Filter::Nearest;
    sampDesc.MagFilter     = Filter::Nearest;
    sampDesc.AddressU      = AddressMode::ClampToEdge;
    sampDesc.AddressV      = AddressMode::ClampToEdge;
    sampDesc.DebugName     = "default_normal_sampler";
    _defaultNormalSampler = _device->CreateSampler(sampDesc);
}

void RenderThread::CreateSpritePipeline(const Format swapchainFormat)
{
    auto vertSpv = LoadShaderFile("shaders/sprite.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/sprite.frag.spv");

    const ShaderHandle vert =
        _device->CreateShader({ ShaderStage::Vertex, vertSpv.data(), static_cast<u32>(vertSpv.size()), "main", "sprite.vert" });
    const ShaderHandle frag = _device->CreateShader(
        { ShaderStage::Fragment, fragSpv.data(), static_cast<u32>(fragSpv.size()), "main", "sprite.frag" });

    // Four vec4 instance attributes — each 16 bytes, packed in one binding.
    // PerInstance = true → VK_VERTEX_INPUT_RATE_INSTANCE (one entry per sprite).
    const VertexAttribute attrs[] = {
        { 0, 0, Format::RGBA32_Float,  0 }, // a_PosSize  (posX, posY, halfW, halfH)
        { 1, 0, Format::RGBA32_Float, 16 }, // a_UV       (u0, v0, u1, v1)
        { 2, 0, Format::RGBA32_Float, 32 }, // a_Tint     (r, g, b, a)
        { 3, 0, Format::RGBA32_Float, 48 }, // a_Rot      (rotCos, rotSin, _, _)
    };
    const VertexBinding bindings[] = {
        { 0, static_cast<u32>(sizeof(SpriteInstance)), true }, // per-instance
    };

    PipelineDesc desc{};
    desc.VertexShader     = vert;
    desc.FragmentShader   = frag;
    desc.Attributes       = attrs;
    desc.Bindings         = bindings;
    desc.CullMode         = CullMode::None;
    desc.ColorFormat      = swapchainFormat;
    desc.DepthFormat      = Format::Undefined;
    desc.UseTextures      = true; // set 0 = albedo
    desc.UseNormalTexture = true; // set 1 = normal map
    desc.UseStorageBuffer = true; // set 2 = light SSBO
    desc.UseShadowBuffer  = true; // set 3 = shadow map SSBO
    // Standard premultiplied-alpha blend for sprite transparency.
    desc.Blend.Enable   = true;
    desc.Blend.SrcColor = BlendFactor::SrcAlpha;
    desc.Blend.DstColor = BlendFactor::OneMinusSrcAlpha;
    desc.Blend.ColorOp  = BlendOp::Add;
    desc.Blend.SrcAlpha = BlendFactor::One;
    desc.Blend.DstAlpha = BlendFactor::Zero;
    desc.Blend.AlphaOp  = BlendOp::Add;
    desc.DebugName      = "SpritePipeline";

    _spritePipeline = _device->CreatePipeline(desc);
    ARCBIT_ASSERT(_spritePipeline.IsValid(), "RenderThread: failed to create sprite pipeline");

    _device->DestroyShader(vert);
    _device->DestroyShader(frag);
}

void RenderThread::CreateInstanceBuffers()
{
    static constexpr u32 InitialCapacity = 256;
    _instanceBufferCapacity             = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(SpriteInstance);
    desc.Usage       = BufferUsage::Vertex;
    desc.HostVisible = true;
    desc.DebugName   = "sprite_instances";

    for (auto& buf : _instanceBuffers)
        buf = _device->CreateBuffer(desc);
}

void RenderThread::CreateLightSSBOs()
{
    static constexpr u32 InitialCapacity = 64;
    _lightSSBOCapacity                  = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(GpuPointLight);
    desc.Usage       = BufferUsage::Storage;
    desc.HostVisible = true;
    desc.DebugName   = "light_ssbo";

    for (auto& ssbo : _lightSSBO)
        ssbo = _device->CreateBuffer(desc);
}

void RenderThread::CreateShadowSSBOs()
{
    // Fixed size — MaxShadowLights rows of ShadowResolution floats each.
    BufferDesc desc{};
    desc.Size        = MaxShadowLights * ShadowResolution * sizeof(f32);
    desc.Usage       = BufferUsage::Storage;
    desc.HostVisible = true;
    desc.DebugName   = "shadow_ssbo";

    for (auto& ssbo : _shadowSSBO)
        ssbo = _device->CreateBuffer(desc);
}

void RenderThread::CreateSDFPipeline(const Format swapchainFormat)
{
    auto vertSpv = LoadShaderFile("shaders/sdf.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/sdf.frag.spv");

    const ShaderHandle vert = _device->CreateShader(
        { ShaderStage::Vertex,   vertSpv.data(), static_cast<u32>(vertSpv.size()), "main", "sdf.vert" });
    const ShaderHandle frag = _device->CreateShader(
        { ShaderStage::Fragment, fragSpv.data(), static_cast<u32>(fragSpv.size()), "main", "sdf.frag" });

    const VertexAttribute attrs[] = {
        { 0, 0, Format::RGBA32_Float,  0 },
        { 1, 0, Format::RGBA32_Float, 16 },
        { 2, 0, Format::RGBA32_Float, 32 },
        { 3, 0, Format::RGBA32_Float, 48 },
    };
    const VertexBinding bindings[] = {
        { 0, static_cast<u32>(sizeof(SpriteInstance)), true },
    };

    PipelineDesc desc{};
    desc.VertexShader   = vert;
    desc.FragmentShader = frag;
    desc.Attributes     = attrs;
    desc.Bindings       = bindings;
    desc.CullMode       = CullMode::None;
    desc.ColorFormat    = swapchainFormat;
    desc.DepthFormat    = Format::Undefined;
    desc.UseTextures    = true; // set 0 = font atlas
    desc.Blend.Enable   = true;
    desc.Blend.SrcColor = BlendFactor::SrcAlpha;
    desc.Blend.DstColor = BlendFactor::OneMinusSrcAlpha;
    desc.Blend.ColorOp  = BlendOp::Add;
    desc.Blend.SrcAlpha = BlendFactor::One;
    desc.Blend.DstAlpha = BlendFactor::Zero;
    desc.Blend.AlphaOp  = BlendOp::Add;
    desc.DebugName      = "SDFPipeline";

    _sdfPipeline = _device->CreatePipeline(desc);
    ARCBIT_ASSERT(_sdfPipeline.IsValid(), "RenderThread: failed to create SDF pipeline");

    _device->DestroyShader(vert);
    _device->DestroyShader(frag);
}

void RenderThread::CreateSDFInstanceBuffers()
{
    static constexpr u32 InitialCapacity = 128;
    _sdfInstanceBufferCapacity           = InitialCapacity;

    BufferDesc desc{};
    desc.Size        = InitialCapacity * sizeof(SpriteInstance);
    desc.Usage       = BufferUsage::Vertex;
    desc.HostVisible = true;
    desc.DebugName   = "sdf_instances";

    for (auto& buf : _sdfInstanceBuffers)
        buf = _device->CreateBuffer(desc);
}

void RenderThread::CreateUIPipeline(const Format swapchainFormat)
{
    auto vertSpv = LoadShaderFile("shaders/ui.vert.spv");
    auto fragSpv = LoadShaderFile("shaders/ui.frag.spv");

    const ShaderHandle vert = _device->CreateShader(
        { ShaderStage::Vertex,   vertSpv.data(), static_cast<u32>(vertSpv.size()), "main", "ui.vert" });
    const ShaderHandle frag = _device->CreateShader(
        { ShaderStage::Fragment, fragSpv.data(), static_cast<u32>(fragSpv.size()), "main", "ui.frag" });

    const VertexAttribute attrs[] = {
        { 0, 0, Format::RGBA32_Float,  0 },
        { 1, 0, Format::RGBA32_Float, 16 },
        { 2, 0, Format::RGBA32_Float, 32 },
        { 3, 0, Format::RGBA32_Float, 48 },
    };
    const VertexBinding bindings[] = {
        { 0, static_cast<u32>(sizeof(SpriteInstance)), true },
    };

    PipelineDesc desc{};
    desc.VertexShader   = vert;
    desc.FragmentShader = frag;
    desc.Attributes     = attrs;
    desc.Bindings       = bindings;
    desc.CullMode       = CullMode::None;
    desc.ColorFormat    = swapchainFormat;
    desc.DepthFormat    = Format::Undefined;
    desc.UseTextures    = true;
    desc.Blend.Enable   = true;
    desc.Blend.SrcColor = BlendFactor::SrcAlpha;
    desc.Blend.DstColor = BlendFactor::OneMinusSrcAlpha;
    desc.Blend.ColorOp  = BlendOp::Add;
    desc.Blend.SrcAlpha = BlendFactor::One;
    desc.Blend.DstAlpha = BlendFactor::Zero;
    desc.Blend.AlphaOp  = BlendOp::Add;
    desc.DebugName      = "UIPipeline";

    _uiPipeline = _device->CreatePipeline(desc);
    ARCBIT_ASSERT(_uiPipeline.IsValid(), "RenderThread: failed to create UI pipeline");
    _device->DestroyShader(vert);
    _device->DestroyShader(frag);
}

void RenderThread::CreateUIResources()
{
    // 1×1 white RGBA8 texture — bound for solid-color UI quads so the shader
    // multiplies (1,1,1,1) × tint = tint, giving the desired solid color.
    TextureDesc texDesc{};
    texDesc.Width     = 1;
    texDesc.Height    = 1;
    texDesc.Format    = Format::RGBA8_UNorm;
    texDesc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    texDesc.DebugName = "ui_white";
    _uiWhiteTex       = _device->CreateTexture(texDesc);

    const u8 white[4] = { 255, 255, 255, 255 };
    _device->UploadTexture(_uiWhiteTex, white, sizeof(white));

    SamplerDesc sampDesc{};
    sampDesc.MinFilter = Filter::Nearest;
    sampDesc.MagFilter = Filter::Nearest;
    sampDesc.AddressU  = AddressMode::ClampToEdge;
    sampDesc.AddressV  = AddressMode::ClampToEdge;
    sampDesc.DebugName = "ui_white_sampler";
    _uiWhiteSampler    = _device->CreateSampler(sampDesc);

    static constexpr u32 InitialCapacity = 256;
    _uiInstanceBufferCapacity            = InitialCapacity;

    BufferDesc bufDesc{};
    bufDesc.Size        = InitialCapacity * sizeof(SpriteInstance);
    bufDesc.Usage       = BufferUsage::Vertex;
    bufDesc.HostVisible = true;
    bufDesc.DebugName   = "ui_instances";

    for (auto& buf : _uiInstanceBuffers)
        buf = _device->CreateBuffer(bufDesc);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void RenderThread::Start(RenderDevice* device, const Format swapchainFormat)
{
    _device = device;

    CreateDefaultNormalResources();
    CreateSpritePipeline(swapchainFormat);
    CreateInstanceBuffers();
    CreateLightSSBOs();
    CreateShadowSSBOs();
    CreateSDFPipeline(swapchainFormat);
    CreateSDFInstanceBuffers();
    CreateUIPipeline(swapchainFormat);
    CreateUIResources();

    _running = true;
    _thread  = std::thread(&RenderThread::Run, this);
    LOG_INFO(Render, "Render thread started");
}

void RenderThread::Stop()
{
    {
        std::lock_guard lock(_mutex);
        _running = false;
    }
    _frameReady.notify_one();

    if (_thread.joinable())
        _thread.join();

    // GPU must be idle before any resources are freed. WaitIdle() here (rather
    // than in Application::Run) keeps the GPU-sync responsibility with the
    // render thread that submitted the work.
    _device->WaitIdle();

    for (auto& ssbo : _lightSSBO)
        if (ssbo.IsValid())
            _device->DestroyBuffer(ssbo);

    for (auto& ssbo : _shadowSSBO)
        if (ssbo.IsValid())
            _device->DestroyBuffer(ssbo);

    for (auto& buf : _instanceBuffers)
        if (buf.IsValid())
            _device->DestroyBuffer(buf);

    for (auto& buf : _sdfInstanceBuffers)
        if (buf.IsValid())
            _device->DestroyBuffer(buf);

    for (auto& buf : _uiInstanceBuffers)
        if (buf.IsValid())
            _device->DestroyBuffer(buf);

    if (_uiWhiteSampler.IsValid()) _device->DestroySampler(_uiWhiteSampler);
    if (_uiWhiteTex.IsValid())     _device->DestroyTexture(_uiWhiteTex);
    if (_uiPipeline.IsValid())     _device->DestroyPipeline(_uiPipeline);
    if (_sdfPipeline.IsValid())    _device->DestroyPipeline(_sdfPipeline);
    if (_spritePipeline.IsValid())
        _device->DestroyPipeline(_spritePipeline);
    if (_defaultNormalSampler.IsValid())
        _device->DestroySampler(_defaultNormalSampler);
    if (_defaultNormalTex.IsValid())
        _device->DestroyTexture(_defaultNormalTex);

    LOG_INFO(Render, "Render thread stopped");
}

// ---------------------------------------------------------------------------
// Frame submission (game thread side)
// ---------------------------------------------------------------------------

void RenderThread::SubmitFrame(FramePacket packet)
{
    std::unique_lock lock(_mutex);
    _frameDone.wait(lock, [this] { return _slotFree; });

    _packet   = std::move(packet);
    _hasFrame = true;
    _slotFree = false;

    lock.unlock();
    _frameReady.notify_one();
}

void RenderThread::WaitIdle()
{
    {
        std::unique_lock lock(_mutex);
        _frameDone.wait(lock, [this] { return _slotFree; });
    }
    // The render thread is now parked waiting for a packet, so this is the
    // only thread using the device.
    if (_device) _device->WaitIdle();
}

RenderStats RenderThread::GetStats() const
{
    return {
        0,
        _statSpritesSubmitted.load(std::memory_order_relaxed),
        _statSpriteBatches.load(std::memory_order_relaxed),
        _statDrawCalls.load(std::memory_order_relaxed), 1, 1,1,
        _statLightsActive.load(std::memory_order_relaxed),
    };
}

// ---------------------------------------------------------------------------
// Render thread loop
// ---------------------------------------------------------------------------

void RenderThread::Run()
{
    while (true)
    {
        FramePacket packet;
        {
            std::unique_lock lock(_mutex);
            _frameReady.wait(lock, [this] { return _hasFrame || !_running; });
            if (!_running && !_hasFrame)
                break;
            packet     = std::move(_packet);
            _hasFrame = false;
        }

        RenderFrame(packet);

        {
            std::lock_guard lock(_mutex);
            _slotFree = true;
        }
        _frameDone.notify_one();
    }
}

// ---------------------------------------------------------------------------
// RenderFrame() helpers
// ---------------------------------------------------------------------------

TextureHandle RenderThread::AcquireBackBuffer(const FramePacket& packet) const
{
    if (packet.NeedsResize)
        _device->ResizeSwapchain(packet.Swapchain, packet.Width, packet.Height);

    TextureHandle backBuffer = _device->AcquireNextImage(packet.Swapchain);
    if (backBuffer.IsValid())
        return backBuffer;

    // Swapchain is out of date (e.g. during fullscreen exit) but the packet
    // dimensions are stale — SDL fires the resize event a few ms later on the
    // game thread, so calling ResizeSwapchain here would build a swapchain at
    // the wrong size, trigger vkDeviceWaitIdle, and deadlock SubmitFrame.
    // Skip the frame and let the NeedsResize packet fix it on the next frame.
    LOG_WARN(Render, "Swapchain out of date at acquire - skipping frame, waiting for resize");
    return {};
}

u32 RenderThread::UploadLights(const FramePacket& packet, const u32 frameSlot)
{
    const u32 lightCount = static_cast<u32>(packet.Lights.size());

    if (lightCount > _lightSSBOCapacity)
    {
        _device->WaitIdle();
        u32 newCapacity = _lightSSBOCapacity;
        while (newCapacity < lightCount)
            newCapacity *= 2;
        _lightSSBOCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(GpuPointLight);
        desc.Usage       = BufferUsage::Storage;
        desc.HostVisible = true;
        desc.DebugName   = "light_ssbo";

        for (auto& ssbo : _lightSSBO)
        {
            _device->DestroyBuffer(ssbo);
            ssbo = _device->CreateBuffer(desc);
        }
        LOG_INFO(Render, "Light SSBO resized to {} entries", newCapacity);
    }

    if (lightCount > 0)
    {
        // Build shadow row lookup: Lights[i] → SSBO row (-1.0 = no shadow).
        std::vector<f32> shadowRows(lightCount, -1.0f);
        for (u32 row = 0; row < packet.ShadowMaps.size() && row < MaxShadowLights; ++row)
            shadowRows[packet.ShadowMaps[row].LightIndex] = static_cast<f32>(row);

        std::vector<GpuPointLight> gpuLights;
        gpuLights.reserve(lightCount);
        for (u32 i = 0; i < lightCount; ++i)
        {
            const PointLight& l = packet.Lights[i];
            GpuPointLight&    g = gpuLights.emplace_back();
            g.PosX      = l.Position.X;
            g.PosY      = l.Position.Y;
            g.Radius    = l.Radius;
            g.Intensity = l.Intensity;
            g.ColorR    = l.LightColor.R;
            g.ColorG    = l.LightColor.G;
            g.ColorB    = l.LightColor.B;
            g.ShadowRow = shadowRows[i];
        }
        _device->UpdateBuffer(_lightSSBO[frameSlot], gpuLights.data(), lightCount * sizeof(GpuPointLight));
    }

    return lightCount;
}

void RenderThread::UploadShadowData(const FramePacket& packet, const u32 frameSlot)
{
    // Always write the full fixed-size block. Unused rows are filled with a large
    // sentinel so the shader's distance comparison never fires for empty rows.
    static constexpr u64 TotalSize = MaxShadowLights * ShadowResolution * sizeof(f32);
    static constexpr f32 NoOccluder = 1e9f;

    std::array<f32, MaxShadowLights * ShadowResolution> data;
    data.fill(NoOccluder);

    const u32 mapCount = std::min(static_cast<u32>(packet.ShadowMaps.size()), MaxShadowLights);
    for (u32 row = 0; row < mapCount; ++row)
    {
        const ShadowMapData& sm = packet.ShadowMaps[row];
        std::memcpy(&data[row * ShadowResolution], sm.Distances, ShadowResolution * sizeof(f32));
    }

    _device->UpdateBuffer(_shadowSSBO[frameSlot], data.data(), TotalSize);
}

std::vector<const Sprite*> RenderThread::SortSprites(const std::vector<Sprite>& sprites)
{
    std::vector<const Sprite*> sorted;
    sorted.reserve(sprites.size());
    for (const Sprite& s : sprites)
        sorted.push_back(&s);

    std::ranges::stable_sort(sorted, [](const Sprite* a, const Sprite* b) {
        if (a->Layer != b->Layer)
            return a->Layer < b->Layer;
        if (a->Texture.Index() != b->Texture.Index())
            return a->Texture.Index() < b->Texture.Index();
        return a->Sampler.Index() < b->Sampler.Index();
    });

    return sorted;
}

std::vector<const Sprite*> RenderThread::SortUISprites(const std::vector<Sprite>& sprites)
{
    std::vector<const Sprite*> sorted;
    sorted.reserve(sprites.size());
    for (const Sprite& s : sprites) sorted.push_back(&s);

    std::ranges::stable_sort(sorted, [](const Sprite* a, const Sprite* b) {
        if (a->Layer      != b->Layer)      return a->Layer      < b->Layer;
        if (a->ClipIndex  != b->ClipIndex)  return a->ClipIndex  < b->ClipIndex;
        if (a->Texture.Index() != b->Texture.Index())
            return a->Texture.Index() < b->Texture.Index();
        if (a->Sampler.Index() != b->Sampler.Index())
            return a->Sampler.Index() < b->Sampler.Index();
        return static_cast<int>(a->SDFMode) < static_cast<int>(b->SDFMode);
    });
    return sorted;
}

void RenderThread::UploadInstances(const std::vector<const Sprite*>& sorted, const u32 frameSlot)
{
    const u32 spriteCount = static_cast<u32>(sorted.size());
    if (spriteCount == 0)
        return;

    if (spriteCount > _instanceBufferCapacity)
    {
        _device->WaitIdle();
        u32 newCapacity = _instanceBufferCapacity;
        while (newCapacity < spriteCount)
            newCapacity *= 2;
        _instanceBufferCapacity = newCapacity;

        BufferDesc desc{};
        desc.Size        = newCapacity * sizeof(SpriteInstance);
        desc.Usage       = BufferUsage::Vertex;
        desc.HostVisible = true;
        desc.DebugName   = "sprite_instances";

        for (auto& buf : _instanceBuffers)
        {
            _device->DestroyBuffer(buf);
            buf = _device->CreateBuffer(desc);
        }
        LOG_INFO(Render, "Instance buffer resized to {} sprites", newCapacity);
    }

    std::vector<SpriteInstance> instances;
    instances.reserve(spriteCount);
    for (const Sprite* s : sorted)
    {
        SpriteInstance& inst = instances.emplace_back();
        inst.PosX            = s->Position.X;
        inst.PosY            = s->Position.Y;
        inst.HalfW           = s->Size.X * 0.5f;
        inst.HalfH           = s->Size.Y * 0.5f;
        inst.U0              = s->UV.U0;
        inst.V0              = s->UV.V0;
        inst.U1              = s->UV.U1;
        inst.V1              = s->UV.V1;
        inst.TintR           = s->Tint.R;
        inst.TintG           = s->Tint.G;
        inst.TintB           = s->Tint.B;
        inst.TintA           = s->Tint.A;
        inst.RotCos          = std::cos(s->Rotation);
        inst.RotSin          = std::sin(s->Rotation);
        inst._pad0           = 0.0f;
        inst._pad1           = 0.0f;
    }
    _device->UpdateBuffer(_instanceBuffers[frameSlot], instances.data(), spriteCount * sizeof(SpriteInstance));
}

RenderThread::LetterboxRect RenderThread::ComputeLetterbox(const FramePacket& packet)
{
    const f32 actualW = static_cast<f32>(packet.Width);
    const f32 actualH = static_cast<f32>(packet.Height);

    if (packet.ReferenceSize.X <= 0.0f || packet.ReferenceSize.Y <= 0.0f || actualH == 0.0f)
        return { 0.0f, 0.0f, actualW, actualH };

    const f32 designAspect = packet.ReferenceSize.X / packet.ReferenceSize.Y;
    const f32 actualAspect = actualW / actualH;

    if (actualAspect > designAspect)
    {
        // Window is wider — pillarbox (bars on left and right).
        const f32 w = actualH * designAspect;
        return { std::round((actualW - w) * 0.5f), 0.0f, std::round(w), actualH };
    }
    else
    {
        // Window is taller — letterbox (bars on top and bottom).
        const f32 h = actualW / designAspect;
        return { 0.0f, std::round((actualH - h) * 0.5f), actualW, std::round(h) };
    }
}

void RenderThread::BeginRenderPass(const CommandListHandle cmd, const TextureHandle backBuffer,
                                   const FramePacket& packet, const LetterboxRect& lb) const
{
    // Clear the full framebuffer to LetterboxColor (fills bars and interior).
    Attachment colorAttach{};
    colorAttach.Texture    = backBuffer;
    colorAttach.Load       = LoadOp::Clear;
    colorAttach.Store      = StoreOp::Store;
    colorAttach.ClearColor = packet.LetterboxColor;

    std::array<Attachment, 1> attachments = { colorAttach };
    RenderingDesc             renderDesc{};
    renderDesc.ColorAttachments = attachments;
    _device->BeginRendering(cmd, renderDesc);

    // If the game interior has a different clear color, fill the letterbox rect.
    const bool hasBars = lb.X > 0.5f || lb.Y > 0.5f;
    if (hasBars || packet.ClearColor != packet.LetterboxColor)
    {
        _device->ClearColorRect(cmd, packet.ClearColor,
                                static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                                static_cast<u32>(lb.W), static_cast<u32>(lb.H));
    }
}

u32 RenderThread::DrawSpriteBatches(const CommandListHandle cmd, const std::vector<const Sprite*>& sorted,
                                    const u32 frameSlot, const FramePacket& packet,
                                    const u32 lightCount, const LetterboxRect& lb) const
{
    const u32 spriteCount = static_cast<u32>(sorted.size());
    if (spriteCount == 0)
        return 0;

    _device->BindPipeline(cmd, _spritePipeline);
    _device->SetViewport(cmd, lb.X, lb.Y, lb.W, lb.H);
    _device->SetScissor(cmd, static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                        static_cast<u32>(lb.W), static_cast<u32>(lb.H));
    _device->BindStorageBuffer(cmd, _lightSSBO[frameSlot],   2, 0);
    _device->BindStorageBuffer(cmd, _shadowSSBO[frameSlot],  3, 0);

    // Use the reference resolution for the NDC transform when set, so the same world
    // area is always visible regardless of window size. Fall back to the letterbox
    // rect dimensions (which equal the full window when no reference is set).
    const f32 refW = (packet.ReferenceSize.X > 0.0f) ? packet.ReferenceSize.X : lb.W;
    const f32 refH = (packet.ReferenceSize.Y > 0.0f) ? packet.ReferenceSize.Y : lb.H;

    SpritePushConstants pc{};
    pc.CamPosX    = packet.CameraPosition.X;
    pc.CamPosY    = packet.CameraPosition.Y;
    pc.ViewportW  = refW / packet.CameraZoom;
    pc.ViewportH  = refH / packet.CameraZoom;
    pc.RotCos     = std::cos(packet.CameraRotation);
    pc.RotSin     = std::sin(packet.CameraRotation);
    pc.AmbientR   = packet.AmbientColor.R;
    pc.AmbientG   = packet.AmbientColor.G;
    pc.AmbientB   = packet.AmbientColor.B;
    pc.AmbientA   = packet.AmbientColor.A;
    pc.LightCount = lightCount;
    _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &pc, sizeof(pc));

    u32 batchCount = 0;
    u32 groupStart = 0;
    while (groupStart < spriteCount)
    {
        const Sprite* first    = sorted[groupStart];
        u32           groupEnd = groupStart + 1;

        while (groupEnd < spriteCount)
        {
            const Sprite* next = sorted[groupEnd];
            if (next->Texture.Index() != first->Texture.Index() || next->Sampler.Index() != first->Sampler.Index() ||
                next->NormalTexture.Index() != first->NormalTexture.Index())
                break;
            ++groupEnd;
        }

        // Albedo — set 0.
        _device->BindTexture(cmd, first->Texture, first->Sampler, 0);

        // Normal map — set 1. Fall back to the flat-normal default.
        const bool    hasNormal     = first->NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? first->NormalTexture : _defaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? first->Sampler : _defaultNormalSampler;
        _device->BindTexture(cmd, normalTex, normalSampler, 1);

        // Offset into the instance buffer for this group's first sprite.
        _device->BindVertexBuffer(cmd, _instanceBuffers[frameSlot], 0, static_cast<u64>(groupStart) * sizeof(SpriteInstance));

        _device->Draw(cmd, 6, 0, groupEnd - groupStart, 0);
        ++batchCount;
        groupStart = groupEnd;
    }

    return batchCount;
}

void RenderThread::DrawLegacyCalls(const CommandListHandle cmd, const FramePacket& packet,
                                   const u32 frameSlot, const u32 lightCount,
                                   const LetterboxRect& lb) const
{
    for (const DrawCall& dc : packet.DrawCalls)
    {
        _device->BindPipeline(cmd, dc.Pipeline);
        _device->SetViewport(cmd, lb.X, lb.Y, lb.W, lb.H);
        _device->SetScissor(cmd, static_cast<i32>(lb.X), static_cast<i32>(lb.Y),
                            static_cast<u32>(lb.W), static_cast<u32>(lb.H));
        _device->BindStorageBuffer(cmd, _lightSSBO[frameSlot], 2, 0);

        if (dc.Texture.IsValid())
            _device->BindTexture(cmd, dc.Texture, dc.Sampler, 0);

        const bool    hasNormal     = dc.NormalTexture.IsValid();
        const TextureHandle normalTex     = hasNormal ? dc.NormalTexture : _defaultNormalTex;
        const SamplerHandle normalSampler = hasNormal ? dc.Sampler : _defaultNormalSampler;
        _device->BindTexture(cmd, normalTex, normalSampler, 1);

        ForwardPushConstants fpc{};
        fpc.PositionX  = dc.Position.X;
        fpc.PositionY  = dc.Position.Y;
        fpc.ScaleX     = dc.Scale.X;
        fpc.ScaleY     = dc.Scale.Y;
        fpc.U0         = dc.UV.U0;
        fpc.V0         = dc.UV.V0;
        fpc.U1         = dc.UV.U1;
        fpc.V1         = dc.UV.V1;
        fpc.AmbientR   = packet.AmbientColor.R;
        fpc.AmbientG   = packet.AmbientColor.G;
        fpc.AmbientB   = packet.AmbientColor.B;
        fpc.AmbientA   = packet.AmbientColor.A;
        fpc.LightCount = lightCount;
        _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &fpc, sizeof(fpc));

        _device->Draw(cmd, dc.VertexCount);
    }
}

void RenderThread::UploadSDFInstances(const std::vector<const Sprite*>& sorted, const u32 frameSlot)
{
    const u32 count = static_cast<u32>(sorted.size());
    if (count == 0) return;

    if (count > _sdfInstanceBufferCapacity) {
        _device->WaitIdle();
        u32 cap = _sdfInstanceBufferCapacity;
        while (cap < count) cap *= 2;
        _sdfInstanceBufferCapacity = cap;

        BufferDesc desc{};
        desc.Size        = cap * sizeof(SpriteInstance);
        desc.Usage       = BufferUsage::Vertex;
        desc.HostVisible = true;
        desc.DebugName   = "sdf_instances";
        for (auto& buf : _sdfInstanceBuffers) { _device->DestroyBuffer(buf); buf = _device->CreateBuffer(desc); }
        LOG_INFO(Render, "SDF instance buffer resized to {} quads", cap);
    }

    std::vector<SpriteInstance> instances;
    instances.reserve(count);
    for (const Sprite* s : sorted) {
        SpriteInstance& inst = instances.emplace_back();
        inst.PosX   = s->Position.X; inst.PosY  = s->Position.Y;
        inst.HalfW  = s->Size.X * 0.5f; inst.HalfH = s->Size.Y * 0.5f;
        inst.U0     = s->UV.U0; inst.V0 = s->UV.V0; inst.U1 = s->UV.U1; inst.V1 = s->UV.V1;
        inst.TintR  = s->Tint.R; inst.TintG = s->Tint.G; inst.TintB = s->Tint.B; inst.TintA = s->Tint.A;
        inst.RotCos = 1.0f; inst.RotSin = 0.0f; inst._pad0 = 0.0f; inst._pad1 = 0.0f;
    }
    _device->UpdateBuffer(_sdfInstanceBuffers[frameSlot], instances.data(), count * sizeof(SpriteInstance));
}

void RenderThread::UploadUIInstances(const std::vector<const Sprite*>& sorted, const u32 frameSlot)
{
    const u32 count = static_cast<u32>(sorted.size());
    if (count == 0) return;

    if (count > _uiInstanceBufferCapacity) {
        _device->WaitIdle();
        u32 cap = _uiInstanceBufferCapacity;
        while (cap < count) cap *= 2;
        _uiInstanceBufferCapacity = cap;

        BufferDesc desc{};
        desc.Size        = cap * sizeof(SpriteInstance);
        desc.Usage       = BufferUsage::Vertex;
        desc.HostVisible = true;
        desc.DebugName   = "ui_instances";
        for (auto& buf : _uiInstanceBuffers) { _device->DestroyBuffer(buf); buf = _device->CreateBuffer(desc); }
        LOG_INFO(Render, "UI instance buffer resized to {} quads", cap);
    }

    std::vector<SpriteInstance> instances;
    instances.reserve(count);
    for (const Sprite* s : sorted) {
        SpriteInstance& inst = instances.emplace_back();
        inst.PosX  = s->Position.X; inst.PosY  = s->Position.Y;
        inst.HalfW = s->Size.X * 0.5f; inst.HalfH = s->Size.Y * 0.5f;
        inst.U0    = s->UV.U0; inst.V0 = s->UV.V0; inst.U1 = s->UV.U1; inst.V1 = s->UV.V1;
        inst.TintR = s->Tint.R; inst.TintG = s->Tint.G; inst.TintB = s->Tint.B; inst.TintA = s->Tint.A;
        inst.RotCos = 1.0f; inst.RotSin = 0.0f;
        inst._pad0  = s->SDFMode ? 1.0f : 0.0f; // mode flag read by ui.vert / ui.frag
        inst._pad1  = 0.0f;
    }
    _device->UpdateBuffer(_uiInstanceBuffers[frameSlot], instances.data(), count * sizeof(SpriteInstance));
}

void RenderThread::DrawSDFBatches(const CommandListHandle cmd, const std::vector<const Sprite*>& sorted,
                                  const FramePacket& packet, const u32 frameSlot) const
{
    if (sorted.empty()) return;

    _device->BindPipeline(cmd, _sdfPipeline);
    // SDF text uses full window space — not constrained to the letterbox rect.
    _device->SetViewport(cmd, 0.0f, 0.0f, static_cast<f32>(packet.Width), static_cast<f32>(packet.Height));
    _device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);

    SDFPushConstants pc{ static_cast<f32>(packet.Width), static_cast<f32>(packet.Height) };
    _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &pc, sizeof(pc));

    const u32 count      = static_cast<u32>(sorted.size());
    u32       groupStart = 0;
    while (groupStart < count) {
        const Sprite* first    = sorted[groupStart];
        u32           groupEnd = groupStart + 1;
        while (groupEnd < count &&
               sorted[groupEnd]->Texture.Index() == first->Texture.Index() &&
               sorted[groupEnd]->Sampler.Index() == first->Sampler.Index())
            ++groupEnd;

        _device->BindTexture(cmd, first->Texture, first->Sampler, 0);
        _device->BindVertexBuffer(cmd, _sdfInstanceBuffers[frameSlot], 0,
                                  static_cast<u64>(groupStart) * sizeof(SpriteInstance));
        _device->Draw(cmd, 6, 0, groupEnd - groupStart, 0);
        groupStart = groupEnd;
    }
}

void RenderThread::DrawUIBatches(const CommandListHandle cmd, const std::vector<const Sprite*>& sorted,
                                 const FramePacket& packet, const u32 frameSlot) const
{
    if (sorted.empty()) return;

    _device->BindPipeline(cmd, _uiPipeline);
    _device->SetViewport(cmd, 0.0f, 0.0f, static_cast<f32>(packet.Width), static_cast<f32>(packet.Height));

    SDFPushConstants pc{ static_cast<f32>(packet.Width), static_cast<f32>(packet.Height) };
    _device->PushConstants(cmd, ShaderStage::Vertex | ShaderStage::Fragment, &pc, sizeof(pc));

    u16 activeClip = 0xFFFF; // sentinel — force scissor set on first batch
    const u32 count = static_cast<u32>(sorted.size());
    u32 groupStart  = 0;

    while (groupStart < count) {
        const Sprite* first    = sorted[groupStart];
        u32           groupEnd = groupStart + 1;
        while (groupEnd < count &&
               sorted[groupEnd]->Texture.Index()  == first->Texture.Index() &&
               sorted[groupEnd]->Sampler.Index()  == first->Sampler.Index() &&
               sorted[groupEnd]->SDFMode           == first->SDFMode        &&
               sorted[groupEnd]->ClipIndex         == first->ClipIndex)
            ++groupEnd;

        // Apply scissor rect when ClipIndex changes.
        if (first->ClipIndex != activeClip) {
            activeClip = first->ClipIndex;
            if (activeClip == 0 || activeClip > static_cast<u16>(packet.UIClipRects.size())) {
                _device->SetScissor(cmd, 0, 0, packet.Width, packet.Height);
            } else {
                const UIRect& cr = packet.UIClipRects[activeClip - 1]; // 1-based
                const i32 cx = static_cast<i32>(cr.X);
                const i32 cy = static_cast<i32>(cr.Y);
                const u32 cw = static_cast<u32>(std::max(0.0f, cr.W));
                const u32 ch = static_cast<u32>(std::max(0.0f, cr.H));
                _device->SetScissor(cmd, cx, cy, cw, ch);
            }
        }

        _device->BindTexture(cmd, first->Texture, first->Sampler, 0);
        _device->BindVertexBuffer(cmd, _uiInstanceBuffers[frameSlot], 0,
                                  static_cast<u64>(groupStart) * sizeof(SpriteInstance));
        _device->Draw(cmd, 6, 0, groupEnd - groupStart, 0);
        groupStart = groupEnd;
    }
}

// ---------------------------------------------------------------------------
// RenderFrame — orchestrates one frame on the render thread
// ---------------------------------------------------------------------------

void RenderThread::RenderFrame(const FramePacket& packet)
{
    const TextureHandle backbuffer = AcquireBackBuffer(packet);
    if (!backbuffer.IsValid())
        return;

    const u32 frameSlot  = _device->GetCurrentFrameIndex(packet.Swapchain);
    const u32 lightCount = UploadLights(packet, frameSlot);
    UploadShadowData(packet, frameSlot);

    // Query the actual swapchain extent after any resize that AcquireBackbuffer
    // may have triggered. packet.Width/Height can be stale by one frame (the SDL
    // resize event races with the render thread), which would cause ClearColorRect
    // to request a rect larger than the active render pass.
    u32 actualW = packet.Width, actualH = packet.Height;
    _device->GetSwapchainExtent(packet.Swapchain, actualW, actualH);

    // Build a corrected packet view with the true dimensions for all rect math.
    FramePacket corrected  = packet; // shallow copy — Sprites/Lights are ptrs in sorted/uploaded form
    corrected.Width        = actualW;
    corrected.Height       = actualH;

    const LetterboxRect lb = ComputeLetterbox(corrected);

    const auto sorted    = SortSprites(packet.Sprites);
    const auto sdfSorted = SortSprites(packet.SDFSprites);
    const auto uiSorted  = SortUISprites(packet.UISprites);
    UploadInstances(sorted, frameSlot);
    UploadSDFInstances(sdfSorted, frameSlot);
    UploadUIInstances(uiSorted, frameSlot);

    CommandListHandle cmd = _device->BeginCommandList();
    BeginRenderPass(cmd, backbuffer, corrected, lb);

    const u32 batchCount = DrawSpriteBatches(cmd, sorted, frameSlot, corrected, lightCount, lb);
    DrawLegacyCalls(cmd, corrected, frameSlot, lightCount, lb);
    DrawUIBatches(cmd, uiSorted, corrected, frameSlot);
    DrawSDFBatches(cmd, sdfSorted, corrected, frameSlot);

    _device->EndRendering(cmd);

    _statSpritesSubmitted.store(static_cast<u32>(sorted.size()), std::memory_order_relaxed);
    _statSpriteBatches.store(batchCount, std::memory_order_relaxed);
    _statDrawCalls.store(static_cast<u32>(packet.DrawCalls.size()) + batchCount, std::memory_order_relaxed);
    _statLightsActive.store(lightCount, std::memory_order_relaxed);

    _device->EndCommandList(cmd);
    _device->Submit({ cmd });
    _device->Present(packet.Swapchain);
}

} // namespace Arcbit
