#pragma once

#include <arcbit/render/RenderTypes.h>

#include <initializer_list>
#include <memory>

namespace Arcbit {

// Forward declaration of the Vulkan implementation detail.
// VulkanContext is defined only in the backend DLL — engine and game code
// never see its internals, which keeps all Vulkan headers out of this layer.
class VulkanContext;

// ---------------------------------------------------------------------------
// RenderDevice
//
// The single point of contact between game/engine code and the GPU.
// All GPU resources are identified by typed handles — plain u32 values that
// index into backend-managed pools. No Vulkan types ever cross this boundary.
//
// Lifetime:
//   Created via Arcbit_CreateDevice() at startup.
//   Destroyed via Arcbit_DestroyDevice() before the window is closed.
//   There should be exactly one RenderDevice per application.
//
// Threading:
//   RenderDevice methods are NOT thread-safe by default.
//   Command lists can be built on multiple threads simultaneously (one per
//   thread), but Submit/Present must be called from a single render thread.
// ---------------------------------------------------------------------------
class ARCBIT_RENDER_API RenderDevice
{
public:
    ~RenderDevice();

    // Not copyable — GPU state is not duplicable.
    RenderDevice(const RenderDevice&)            = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    // =========================================================================
    // Buffers
    //
    // Buffers hold arbitrary binary data on the GPU (vertices, indices,
    // uniforms, lights, etc.). The Usage flags determine which pipeline
    // stages can access the buffer and how Vulkan allocates its memory.
    // =========================================================================

    // Allocate a new GPU buffer with the given description.
    // Returns a handle that identifies the buffer in all subsequent calls.
    // The buffer's contents are undefined until UpdateBuffer is called.
    BufferHandle CreateBuffer(const BufferDesc& desc);

    // Write CPU data into a previously created buffer.
    //
    // HostVisible buffers (e.g. uniform / light SSBO):
    //   Data is memcpy'd directly into persistently-mapped memory.
    //   Fast — no GPU synchronisation required.
    //
    // Device-local buffers (e.g. static vertex / index buffers):
    //   A temporary staging buffer is created, the data is copied into it,
    //   then a GPU copy command transfers it to the device-local buffer.
    //   Slower — incurs a GPU flush — but device-local memory is faster to
    //   read from shaders.
    //
    // @param handle  Handle returned by CreateBuffer.
    // @param data    Pointer to the CPU-side source data.
    // @param size    Number of bytes to write.
    // @param offset  Byte offset into the buffer to begin writing (default 0).
    void UpdateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset = 0);

    // Release the buffer and return its memory to the allocator.
    // The handle becomes invalid after this call.
    // GPU work that references this buffer must have completed before calling.
    void DestroyBuffer(BufferHandle handle);

    // Copy bytes out of a host-visible buffer the GPU wrote (BufferUsage::
    // Readback). The GPU work that wrote it must have completed (WaitIdle, or
    // the frame's fence) - this doesn't wait.
    //
    // @param handle  A HostVisible buffer created with BufferUsage::Readback.
    // @param out     Destination; at least `size` bytes.
    // @param size    Number of bytes to read.
    // @param offset  Byte offset into the buffer to begin reading (default 0).
    void ReadBuffer(BufferHandle handle, void* out, u64 size, u64 offset = 0);

    // =========================================================================
    // Textures
    //
    // Textures are 2D images on the GPU. They can be sampled in shaders,
    // used as render targets (G-buffer, light accumulation), or as depth
    // buffers. Usage flags control which roles the texture can fill.
    // =========================================================================

    // Allocate a GPU texture. Contents are undefined until UploadTexture
    // is called or the texture is first written as a render target.
    TextureHandle CreateTexture(const TextureDesc& desc);

    // Upload raw pixel data from the CPU into a texture.
    // Internally uses a staging buffer + GPU copy, similar to device-local
    // buffer uploads. The texture must have been created with Transfer usage.
    //
    // @param data  Tightly-packed pixel data in the texture's format.
    // @param size  Total byte size of the pixel data.
    void UploadTexture(TextureHandle handle, const void* data, u64 size);

    // Release the texture and its GPU memory.
    // GPU work that samples or writes to this texture must have completed.
    void DestroyTexture(TextureHandle handle);

    // =========================================================================
    // Samplers
    //
    // Samplers are small state objects that describe how a texture is
    // filtered and addressed when read in a shader (bilinear, nearest,
    // clamp, wrap, etc.). They are separate from textures so the same
    // sampler can be reused across many textures.
    // =========================================================================

    // Create a sampler with the given filtering and addressing state.
    SamplerHandle CreateSampler(const SamplerDesc& desc);

    // Release the sampler. GPU work using it must have completed.
    void DestroySampler(SamplerHandle handle);

    // =========================================================================
    // Shaders
    //
    // Shaders are compiled from GLSL to SPIR-V at build time (by glslc).
    // The resulting .spv bytecode is loaded and passed here as a byte array.
    // Each shader module wraps one stage (vertex or fragment).
    // =========================================================================

    // Compile a SPIR-V shader module on the device.
    // The bytecode is copied into driver memory — the caller's array can be
    // freed after this returns.
    //
    // @param desc.Code      Pointer to raw SPIR-V bytecode.
    // @param desc.CodeSize  Byte length — must be a multiple of 4.
    ShaderHandle CreateShader(const ShaderDesc& desc);

    // Release the shader module. Pipelines that were created using this
    // shader are NOT affected — Vulkan copies the bytecode during pipeline
    // creation, so the module can be destroyed afterwards.
    void DestroyShader(ShaderHandle handle);

    // =========================================================================
    // Pipelines
    //
    // A pipeline bundles together shaders, vertex input layout, rasterizer
    // state, depth/stencil state, and blend state into a single GPU object.
    // Pipelines are expensive to create and should be built at load time,
    // not per-frame. Each unique combination of state is a separate pipeline.
    // =========================================================================

    // Create a graphics pipeline from the given description.
    // This is a relatively heavy operation — call at load time or scene init.
    PipelineHandle CreatePipeline(const PipelineDesc& desc);

    // Release the pipeline. GPU work using it must have completed.
    void DestroyPipeline(PipelineHandle handle);

    // =========================================================================
    // Swapchain
    //
    // The swapchain manages a ring of images that are presented to the
    // display. Each frame we acquire one image, render into it, then present
    // it. The image we rendered to goes to the display while we start
    // rendering into the next one.
    // =========================================================================

    // Returns the color format of the swapchain as a Format enum value.
    // Pass this to PipelineDesc::ColorFormat so the pipeline matches the
    // actual swapchain format chosen by the driver.
    Format GetSwapchainColorFormat(SwapchainHandle handle);

    // Returns the current pixel dimensions of the swapchain image.
    // Always reflects the post-resize size — safe to call after AcquireNextImage
    // to get the true extent even when a resize just occurred.
    void GetSwapchainExtent(SwapchainHandle handle, u32& outWidth, u32& outHeight);

    // Create a swapchain tied to the given window.
    // The window handle (SDL_Window*) is passed as void* to keep SDL out of
    // this header. The surface created during device init is reused here.
    SwapchainHandle CreateSwapchain(const SwapchainDesc& desc);

    // Recreate the swapchain at a new size (called on window resize).
    // All in-flight frames must be complete before calling.
    void ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height);

    // Release the swapchain and all its images.
    void DestroySwapchain(SwapchainHandle handle);

    // =========================================================================
    // Frame
    // =========================================================================

    // Acquire the next available swapchain image and return it as a texture
    // handle that can be used as a color attachment in BeginRendering.
    // Blocks if no image is currently available (triple-buffering avoids this).
    // Must be called once per frame before any rendering commands.
    TextureHandle AcquireNextImage(SwapchainHandle handle);

    // =========================================================================
    // Command recording
    //
    // Commands are recorded into command lists and then submitted in bulk.
    // This separates the CPU-side description of work from actual GPU dispatch.
    //
    // Typical frame:
    //   CommandListHandle cmd = BeginCommandList();
    //   BeginRendering(cmd, { .ColorAttachments = {...} });
    //   BindPipeline(cmd, pipeline);
    //   BindVertexBuffer(cmd, vb, 0);
    //   Draw(cmd, 3);
    //   EndRendering(cmd);
    //   EndCommandList(cmd);
    //   Submit({ cmd });
    //   Present(swapchain);
    // =========================================================================

    // Allocate a command buffer and begin recording into it.
    // Returns a handle representing the in-progress command list.
    CommandListHandle BeginCommandList();

    // Finish recording and mark the command list as ready for submission.
    void EndCommandList(CommandListHandle handle);

    // Begin a dynamic rendering pass — no VkRenderPass objects required.
    // Describes the color and optional depth attachments to write to.
    // Uses Vulkan 1.3 vkCmdBeginRendering under the hood.
    void BeginRendering(CommandListHandle cmd, const RenderingDesc& desc);

    // End the dynamic rendering pass started by BeginRendering.
    void EndRendering(CommandListHandle cmd);

    // Record a copy of the swapchain image being rendered this frame into
    // `dst` (a HostVisible BufferUsage::Readback buffer of at least width x
    // height x 4 bytes): tightly packed rows, top row first, in the
    // swapchain's format (GetSwapchainColorFormat - usually BGRA8). Call
    // between EndRendering and the next BeginRendering; the image stays ready
    // to present, or to render on with LoadOp::Load. Read `dst` once the
    // frame's GPU work has finished.
    void CopySwapchainImage(CommandListHandle cmd, BufferHandle dst);

    // Bind a pipeline — sets shaders, rasterizer state, depth state, etc.
    // Must be called before any draw commands within a rendering pass.
    void BindPipeline(CommandListHandle cmd, PipelineHandle pipeline);

    // Bind a vertex buffer to a binding slot.
    // The binding number must match the slot declared in the pipeline's
    // VertexBinding array. Multiple buffers can be bound to different slots.
    //
    // @param binding  Vertex buffer binding slot index.
    // @param offset   Byte offset into the buffer to start reading from.
    void BindVertexBuffer(CommandListHandle cmd, BufferHandle buffer,
                          u32 binding, u64 offset = 0);

    // Bind an index buffer for indexed draw calls (DrawIndexed).
    // @param type  U16 or U32 — must match the index data in the buffer.
    void BindIndexBuffer(CommandListHandle cmd, BufferHandle buffer,
                         IndexType type, u64 offset = 0);

    // Bind a texture and sampler pair so shaders can sample it.
    // Updates the texture's pre-allocated descriptor set with the given sampler,
    // then binds it to the pipeline at the specified set index.
    // Must be called after BindPipeline and before the draw that reads the texture.
    //
    // @param set      Descriptor set index (layout(set = N, binding = 0)).
    // @param binding  Binding index within the set (currently always 0).
    void BindTexture(CommandListHandle cmd, TextureHandle texture, SamplerHandle sampler,
                     u32 set = 0, u32 binding = 0);

    // Bind a storage buffer (SSBO) to a descriptor set binding.
    // Used for large read/write data that changes per frame — e.g. the
    // dynamic light list. The shader iterates over lightCount entries
    // instead of a fixed-size array.
    //
    // @param set      Descriptor set index (layout(set = N, binding = M)).
    // @param binding  Binding index within the set.
    void BindStorageBuffer(CommandListHandle cmd, BufferHandle buffer,
                           u32 set, u32 binding);

    // Write push constants — small amounts of per-draw data sent directly
    // through the command buffer without a descriptor set. Ideal for things
    // like the model matrix, light count, or material index. Max 128 bytes
    // guaranteed by the Vulkan spec; 256 bytes on most desktop hardware.
    //
    // @param stages  Which shader stages will read these constants.
    // @param data    Pointer to the data to push.
    // @param size    Byte size of the data.
    // @param offset  Byte offset within the push constant block (default 0).
    void PushConstants(CommandListHandle cmd, ShaderStage stages,
                       const void* data, u32 size, u32 offset = 0);

    // Set the viewport — maps NDC coordinates to window pixels.
    // minDepth/maxDepth control the depth range (0–1 for standard use).
    void SetViewport(CommandListHandle cmd, f32 x, f32 y, f32 width, f32 height,
                     f32 minDepth = 0.0f, f32 maxDepth = 1.0f);

    // Set the scissor rectangle — fragments outside are discarded.
    // Typically matches the viewport unless rendering to a sub-region.
    void SetScissor(CommandListHandle cmd, i32 x, i32 y, u32 width, u32 height);

    // Fill a pixel rectangle with a solid color inside an active render pass.
    // Uses vkCmdClearAttachments — valid inside BeginRendering / EndRendering.
    // Ignores the current scissor state; the rect is in framebuffer coordinates.
    void ClearColorRect(CommandListHandle cmd, Color color, i32 x, i32 y, u32 width, u32 height);

    // Issue a non-indexed draw — reads vertices sequentially from the bound
    // vertex buffer(s). Use for simple geometry or when index buffers would
    // add unnecessary overhead (e.g. the fullscreen triangle trick).
    //
    // @param vertexCount    Number of vertices per instance.
    // @param firstVertex    Starting offset into the vertex buffer.
    // @param instanceCount  Number of instances (default 1).
    // @param firstInstance  Starting instance ID (default 0).
    void Draw(CommandListHandle cmd, u32 vertexCount, u32 firstVertex = 0,
              u32 instanceCount = 1, u32 firstInstance = 0);

    // Issue an indexed draw — reads indices from the bound index buffer,
    // which in turn index into the bound vertex buffer(s). Typically more
    // efficient than non-indexed for geometry with shared vertices.
    //
    // @param indexCount    Number of indices per instance.
    // @param firstIndex    Starting offset into the index buffer.
    // @param vertexOffset  Added to every index before fetching a vertex.
    void DrawIndexed(CommandListHandle cmd, u32 indexCount, u32 firstIndex = 0,
                     i32 vertexOffset = 0, u32 instanceCount = 1, u32 firstInstance = 0);

    // =========================================================================
    // Submit & present
    // =========================================================================

    // Submit one or more completed command lists to the GPU for execution.
    // Commands are executed in the order they appear in the list.
    // Call EndCommandList on each handle before submitting.
    void Submit(std::initializer_list<CommandListHandle> commands);

    // Present the most recently acquired swapchain image to the display.
    // Internally signals the render-finished semaphore and queues a present.
    // Must be called after Submit for the frame that rendered to the
    // swapchain image.
    void Present(SwapchainHandle handle);

    // Block the calling thread until all pending GPU work is complete.
    // Equivalent to vkDeviceWaitIdle. Use only during shutdown or when
    // rebuilding resources that the GPU is currently reading from.
    void WaitIdle();

    // Returns the current per-frame slot index (0 … MaxFramesInFlight-1) for
    // the given swapchain. Use this to index into per-frame CPU arrays (e.g.
    // light SSBOs) so the CPU slot the game thread writes to is never the same
    // slot the GPU is currently reading from.
    u32 GetCurrentFrameIndex(SwapchainHandle handle);

    // Maximum number of frames the CPU is allowed to be ahead of the GPU.
    // Mirrors the value used internally by the backend for frame-slot sizing.
    static constexpr u32 MaxFramesInFlight = 2;

    // -------------------------------------------------------------------------
    // Constructed by Arcbit_CreateDevice only.
    // VulkanContext is forward-declared and never defined in engine headers,
    // so game code cannot call this constructor even though it is public
    // (the argument type is an incomplete type outside the backend).
    // -------------------------------------------------------------------------
    explicit RenderDevice(std::unique_ptr<VulkanContext> context);

private:
    std::unique_ptr<VulkanContext> _context;
};

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Backend factory
//
// extern "C" must live outside any namespace — C linkage and namespaces are
// incompatible. The symbols are still prefixed to avoid collisions.
//
// Arcbit_CreateDevice  — initializes the Vulkan backend and returns a device.
//                        Returns nullptr if initialization fails.
// Arcbit_DestroyDevice — shuts down the device and frees all GPU resources.
//                        Call WaitIdle() before destroying if GPU work may
//                        still be in flight.
// ---------------------------------------------------------------------------
extern "C"
{
    ARCBIT_RENDER_API Arcbit::RenderDevice* Arcbit_CreateDevice(const Arcbit::DeviceDesc& desc);
    ARCBIT_RENDER_API void                  Arcbit_DestroyDevice(Arcbit::RenderDevice* device);
}
