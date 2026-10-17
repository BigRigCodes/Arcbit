#pragma once

#include <arcbit/render/RenderTypes.h>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <array>
#include <vector>
#include <optional>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// Internal Vulkan resource types
//
// These structs mirror the public Handle types but hold the actual Vulkan
// objects. They are stored inside HandlePool<T> and are never exposed beyond
// this backend DLL — engine and game headers only ever see the opaque handles.
// ---------------------------------------------------------------------------

// Maximum number of frames the CPU is allowed to be ahead of the GPU.
// 2 = double-buffering: one frame being rendered, one being prepared.
// Declared before the resource structs so they can size their per-frame arrays.
static constexpr u32 MaxFramesInFlight = 2;

// Represents a GPU buffer — vertex data, index data, uniforms, SSBOs, etc.
// HostVisible buffers are persistently mapped so the CPU can write directly;
// device-local (GPU-only) buffers require a staging copy for uploads.
//
// Storage buffers additionally carry one descriptor set per frame-in-flight
// slot. The render thread cycles through slots each frame, so DescriptorSets[N]
// is always bound to the frame whose GPU work is using slot N.
struct VulkanBuffer
{
    VkBuffer      Buffer      = VK_NULL_HANDLE;
    VmaAllocation Allocation  = VK_NULL_HANDLE;
    u64           Size        = 0;
    BufferUsage   Usage       = BufferUsage::None;
    bool          HostVisible = false;   // true → CPU can write via Mapped
    void*         Mapped      = nullptr; // non-null when HostVisible; VMA keeps it persistently mapped

    // Allocated when Usage includes Storage; one set per frame-in-flight slot.
    // Other buffer types leave these as VK_NULL_HANDLE.
    std::array<VkDescriptorSet, MaxFramesInFlight> DescriptorSets = {};

    // Tracks whether this frame slot's descriptor set has already been written
    // with the current VkBuffer handle. BindStorageBuffer skips
    // vkUpdateDescriptorSets when this is true to avoid invalidating the
    // recording command buffer (the rule VulkanTexture::SamplerSets is built around).
    std::array<bool, MaxFramesInFlight> DescriptorSetWritten = {};
};

// Represents a 2D image on the GPU — textures, render targets, depth buffers.
// Swapchain images are managed by the driver, not VMA, so IsSwapchainImage
// distinguishes them to avoid double-free during cleanup.
struct VulkanTexture
{
    VkImage       Image            = VK_NULL_HANDLE;
    VkImageView   View             = VK_NULL_HANDLE; // required to bind the image in shaders or as attachment
    VmaAllocation Allocation       = VK_NULL_HANDLE; // null for swapchain images (driver-owned)
    VkFormat      Format           = VK_FORMAT_UNDEFINED;
    u32           Width            = 0;
    u32           Height           = 0;
    TextureUsage  Usage            = TextureUsage::None;
    bool          IsSwapchainImage = false; // swapchain images aren't VMA-owned; skip vmaDestroyImage

    // The image's layout as of the last command recorded against it, tracked
    // for swapchain images (BeginRendering / EndRendering / CopySwapchainImage).
    // BeginRendering with LoadOp::Load transitions from this layout instead
    // of UNDEFINED, which would let the driver discard what's already drawn.
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // True when the texture was created with TextureUsage::Sampled, i.e. it
    // may be bound to a shader through BindTexture.
    bool Sampled = false;

    // One descriptor set per sampler this texture has been drawn with.
    //
    // A COMBINED_IMAGE_SAMPLER descriptor bakes the image view *and* the
    // sampler into the set, so a texture drawn with two samplers needs two
    // sets. Each set here is written exactly once - by BindTexture, the
    // first time the texture meets that sampler - and never updated again.
    // A set that is never updated may be bound any number of times in one
    // command buffer and by several frames in flight at once, which is what
    // makes this safe.
    //
    // (The previous design kept one set per frame slot and rewrote it when
    // the sampler changed. Vulkan forbids updating a set that the command
    // buffer being recorded has already bound - the command buffer becomes
    // invalid - so drawing one texture with two samplers in a frame, e.g.
    // the UI's white texture with its own sampler and with a scene's
    // sampler, broke the frame.)
    //
    // Entries for a sampler are freed by DestroySampler (a recycled VkSampler
    // value must never match a set written for the dead one) and all of
    // them by DestroyTexture.
    struct SamplerSet
    {
        VkSampler       Sampler = VK_NULL_HANDLE;
        VkDescriptorSet Set     = VK_NULL_HANDLE;
    };
    std::vector<SamplerSet> SamplerSets;
};

// Represents a sampler — the small state object that describes how a texture
// is filtered (bilinear, nearest) and addressed (clamp, wrap) in shaders.
// Samplers are separate from textures so they can be shared across many images.
struct VulkanSampler
{
    VkSampler Sampler = VK_NULL_HANDLE;
};

// Wraps a VkShaderModule (compiled SPIR-V bytecode loaded into the driver).
// The Stage field lets the pipeline know which stage this module is for.
struct VulkanShader
{
    VkShaderModule Module = VK_NULL_HANDLE;
    ShaderStage    Stage  = ShaderStage::None;
};

// Wraps a VkPipeline and its associated VkPipelineLayout.
// The layout describes the push constant ranges and descriptor set layouts
// that shaders expect — it must be kept alive for as long as the pipeline is used.
struct VulkanPipeline
{
    VkPipeline       Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout Layout   = VK_NULL_HANDLE;
};

// Wraps a VkSwapchainKHR and all its associated per-frame resources.
//
// Sync model (one set per frame in flight):
//   ImageAvailable — signalled by vkAcquireNextImageKHR when the image is ready to render into.
//   RenderFinished — signalled by the queue submit when rendering is done; waited on by present.
//   InFlight       — CPU fence; we wait on this before reusing a frame slot to avoid overwriting
//                    command buffers or resources that the GPU is still reading.
struct VulkanSwapchain
{
    VkSwapchainKHR             Swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR               Surface   = VK_NULL_HANDLE; // the WSI surface this swapchain presents to
    VkFormat                   Format    = VK_FORMAT_UNDEFINED;
    VkExtent2D                 Extent    = {};
    std::vector<VkImage>       Images;       // driver-owned swapchain images
    std::vector<VkImageView>   ImageViews;   // one view per image for use as color attachments
    std::vector<TextureHandle> ImageHandles; // engine-side handle for each swapchain image

    // Per-frame-in-flight synchronisation primitives.
    // Indexed by CurrentFrame (wraps at MaxFramesInFlight).
    std::vector<VkSemaphore> ImageAvailable; // CPU→GPU: "image is ready to be rendered into"
    std::vector<VkSemaphore> RenderFinished; // GPU→GPU: "rendering is done, safe to present"
    std::vector<VkFence>     InFlight;       // GPU→CPU: "frame N is done, you can reuse its resources"

    u32  CurrentImageIndex = 0;    // index into Images[] of the currently acquired swapchain image
    u32  CurrentFrame      = 0;    // index into per-frame sync arrays; wraps at MaxFramesInFlight
    bool VSync             = true; // stored to allow ResizeSwapchain to recreate with same mode

    // The images were created with TRANSFER_SRC usage, so CopySwapchainImage
    // can read a frame back. Set by BuildSwapchain from the surface's caps.
    bool CanCopyFrom = false;
};

// Wraps a VkCommandBuffer allocated from the context's command pool.
// One per in-flight frame; recorded on the CPU and submitted to the GPU queue.
// BoundLayout is updated by BindPipeline so PushConstants can call
// vkCmdPushConstants with the correct layout without needing a separate argument.
struct VulkanCommandList
{
    VkCommandBuffer  Buffer      = VK_NULL_HANDLE;
    VkPipelineLayout BoundLayout = VK_NULL_HANDLE; // set by BindPipeline
};

// ---------------------------------------------------------------------------
// HandlePool<T>
//
// A slot-map with generational indices. Maps public Handle<Tag> values to
// internal resource objects of type T.
//
// Design:
//   - Slot 0 is permanently reserved so index 0 always means "invalid".
//   - Each slot carries a generation counter that is incremented on Free.
//     A handle whose generation doesn't match the slot is stale — it was freed
//     and the slot may now hold a different resource.
//   - Freed slots go onto a free-list and are reused on the next Allocate,
//     keeping the pool compact without shifting indices.
//
// T must be default-constructible.
// ---------------------------------------------------------------------------
template<typename T>
class HandlePool
{
public:
    HandlePool()
    {
        // Reserve slot 0 permanently so index 0 is always the "invalid" sentinel.
        _slots.push_back({});
    }

    // Allocate a slot for the given resource and return a typed handle.
    // Reuses a previously freed slot if one is available; otherwise grows the pool.
    template<typename Tag>
    Handle<Tag> Allocate(T resource)
    {
        u32 index;

        if (!_freeList.empty())
        {
            // Reuse an existing slot — generation was already bumped on Free.
            index = _freeList.back();
            _freeList.pop_back();
        } else
        {
            // Append a fresh slot.
            index = static_cast<u32>(_slots.size());
            _slots.push_back({});
        }

        auto& slot    = _slots[index];
        slot.Resource = std::move(resource);
        slot.Occupied = true;
        // Generation was bumped on the previous Free; keep it as-is to embed it in the handle.
        return Handle<Tag>::Make(index, slot.Generation);
    }

    // Look up a resource by handle.
    // Returns nullptr if the handle is invalid, out of range, or stale (freed and reused).
    template<typename Tag>
    T* Get(Handle<Tag> handle)
    {
        if (!handle.IsValid())
            return nullptr;
        const u32 index = handle.Index();
        if (index >= _slots.size())
            return nullptr;
        auto& slot = _slots[index];
        // Generation mismatch means the handle is stale — the slot holds a different resource.
        if (!slot.Occupied || slot.Generation != handle.Generation())
            return nullptr;
        return &slot.Resource;
    }

    // Free a slot and return the resource so the caller can destroy its GPU objects.
    // The slot's generation is incremented, invalidating all existing handles to it.
    // Returns nullopt if the handle is invalid or already freed.
    template<typename Tag>
    std::optional<T> Free(Handle<Tag> handle)
    {
        if (!handle.IsValid())
            return std::nullopt;
        const u32 index = handle.Index();
        if (index >= _slots.size())
            return std::nullopt;
        auto& slot = _slots[index];
        if (!slot.Occupied || slot.Generation != handle.Generation())
            return std::nullopt;

        T resource    = std::move(slot.Resource);
        slot.Resource = {};
        slot.Occupied = false;
        // Bump generation so any remaining copies of this handle are now stale.
        slot.Generation = (slot.Generation + 1) & Handle<Tag>::GenMask;
        _freeList.push_back(index);
        return resource;
    }

    // Visit every live resource. Used to drop per-texture state that refers
    // to a resource being destroyed (DestroySampler).
    template<typename Fn>
    void ForEach(Fn&& fn)
    {
        for (auto& slot : _slots)
            if (slot.Occupied) fn(slot.Resource);
    }

    // Returns the number of currently allocated (live) resources.
    u32 Count() const
    {
        // Total slots minus the reserved slot 0 and the free-listed slots.
        return static_cast<u32>(_slots.size() - 1 - _freeList.size());
    }

private:
    struct Slot
    {
        T    Resource   = {}; // the stored resource; undefined when !Occupied
        u32  Generation = 0;  // incremented on every Free; embedded in issued handles
        bool Occupied   = false;
    };

    std::vector<Slot> _slots;    // slot 0 is permanently reserved
    std::vector<u32>  _freeList; // indices of freed slots available for reuse
};

// ---------------------------------------------------------------------------
// VulkanContext
//
// Owns all Vulkan state for the lifetime of a RenderDevice.
// Lives behind the Pimpl in RenderDevice — game and engine code never see this
// header because it pulls in <vulkan/vulkan.h>.
//
// Initialisation order (Init):
//   1. CreateInstance      — VkInstance + optional debug messenger
//   2. SDL surface         — needed by SelectPhysicalDevice for present support
//   3. SelectPhysicalDevice — scores all GPUs, picks the best one, records queue families
//   4. CreateLogicalDevice  — VkDevice with dynamic rendering + synchronisation2 enabled
//   5. CreateAllocator      — VMA allocator for buffer/image memory management
//   6. CreateCommandPool    — single pool; will expand to per-thread pools later
//
// Shutdown destroys objects in reverse order after vkDeviceWaitIdle.
// ---------------------------------------------------------------------------
struct VulkanContext
{
    // -------------------------------------------------------------------------
    // Instance-level objects
    // -------------------------------------------------------------------------

    // The Vulkan instance — the connection between the application and the Vulkan
    // loader. All other Vulkan objects are ultimately derived from it.
    VkInstance Instance = VK_NULL_HANDLE;

    // Debug messenger that routes validation layer messages through DebugCallback
    // to the engine's Log system. Only created when EnableValidation is true.
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Physical device
    // -------------------------------------------------------------------------

    // The selected GPU — a VkPhysicalDevice is a handle to the physical hardware.
    // We pick the best one via ScoreDevice (discrete > integrated; must support
    // Vulkan 1.3 + dynamic rendering + synchronization2 + swapchain extension).
    VkPhysicalDevice           PhysicalDevice      = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties PhysicalDeviceProps = {}; // name, limits, type, Vulkan version
    VkPhysicalDeviceFeatures   PhysicalDeviceFeats = {}; // optional feature support flags

    // -------------------------------------------------------------------------
    // Queue families
    // -------------------------------------------------------------------------

    // Vulkan work is submitted to queues. Queue families group queues with the
    // same capabilities. We need one family that supports graphics commands and
    // one that supports presenting images to a surface. They are often the same.
    u32 GraphicsFamily = ~0u; // ~0u = sentinel meaning "not yet assigned"
    u32 PresentFamily  = ~0u;

    // -------------------------------------------------------------------------
    // Logical device + queues
    // -------------------------------------------------------------------------

    // The logical device — our interface to the physical hardware. Created once
    // we know which extensions and features we need. All resource creation calls
    // (vkCreateBuffer, vkCreateImageView, etc.) go through this.
    VkDevice Device = VK_NULL_HANDLE;

    // Handles to the actual queue objects within the logical device.
    // We submit command buffers to GraphicsQueue and present frames via PresentQueue.
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    VkQueue PresentQueue  = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Memory allocator
    // -------------------------------------------------------------------------

    // Vulkan Memory Allocator (VMA) — abstracts GPU heap selection and suballocation.
    // Without VMA we'd have to call vkAllocateMemory for every resource, which
    // quickly hits driver limits. VMA pools allocations and picks the right heap
    // (device-local, host-visible, etc.) based on usage hints.
    VmaAllocator Allocator = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Command infrastructure
    // -------------------------------------------------------------------------

    // Command pool — the backing allocator for command buffers.
    // Created with RESET_COMMAND_BUFFER_BIT so individual command buffers can be
    // reset and re-recorded each frame without resetting the whole pool.
    // This is a single main-thread pool; will expand to per-thread pools later.
    VkCommandPool CommandPool = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Descriptor infrastructure
    //
    // GlobalDescriptorPool is a shared pool from which we allocate:
    //   - One VkDescriptorSet per sampled texture (COMBINED_IMAGE_SAMPLER).
    //   - One VkDescriptorSet per frame-in-flight slot per storage buffer (STORAGE_BUFFER).
    //
    // TextureSetLayout    — set N, binding 0 = combined image sampler, fragment stage.
    //                       Reused for both albedo (set 0) and normal map (set 1) slots.
    // StorageBufferSetLayout — set N, binding 0 = storage buffer, vertex+fragment stages.
    //                          Used for the per-frame dynamic light list (set 2).
    // -------------------------------------------------------------------------
    VkDescriptorPool      GlobalDescriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSetLayout TextureSetLayout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout StorageBufferSetLayout = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Resource pools
    //
    // Each pool maps a typed handle (e.g. BufferHandle) to the corresponding
    // internal Vulkan resource (e.g. VulkanBuffer). Generational indices catch
    // use-after-free bugs at runtime.
    // -------------------------------------------------------------------------
    HandlePool<VulkanBuffer>      Buffers;
    HandlePool<VulkanTexture>     Textures;
    HandlePool<VulkanSampler>     Samplers;
    HandlePool<VulkanShader>      Shaders;
    HandlePool<VulkanPipeline>    Pipelines;
    HandlePool<VulkanSwapchain>   Swapchains;
    HandlePool<VulkanCommandList> CommandLists;

    // -------------------------------------------------------------------------
    // Frame state
    //
    // ActiveSwapchain is set by AcquireNextImage and read by Submit/Present
    // to know which semaphores and fence to use for synchronisation.
    //
    // FrameCommandBuffers are pre-allocated once (MaxFramesInFlight buffers)
    // and cycled each frame. They are reset at the start of each frame after
    // the in-flight fence confirms the GPU is done with that slot.
    // -------------------------------------------------------------------------
    SwapchainHandle                                ActiveSwapchain;
    std::array<VkCommandBuffer, MaxFramesInFlight> FrameCommandBuffers = {};

    // -------------------------------------------------------------------------
    // Deferred surface
    //
    // The WSI surface is created during Init (so SelectPhysicalDevice can verify
    // present support), but it logically belongs to the swapchain. We store it
    // here temporarily and transfer ownership when CreateSwapchain is called.
    // If no swapchain is ever created, Shutdown destroys it.
    // -------------------------------------------------------------------------
    VkSurfaceKHR m_InitSurface = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Lifetime
    // -------------------------------------------------------------------------

    // Initialize all Vulkan state. Returns false on any failure; the caller
    // should log the error and return nullptr from Arcbit_CreateDevice.
    bool Init(const DeviceDesc& desc);

    // Destroy all Vulkan state in reverse-init order.
    // Caller must call vkDeviceWaitIdle (or RenderDevice::WaitIdle) first if
    // any GPU work may still be in flight.
    void Shutdown();

private:
    // Sub-steps called by Init — each returns false on failure.
    bool CreateInstance(const char* appName, u32 appVersion, bool enableValidation);
    bool SelectPhysicalDevice(VkSurfaceKHR surface);
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateCommandPool();
    bool CreateDescriptorPool();
    bool CreateDescriptorSetLayout();

    // Attach the debug messenger to the instance. Called after CreateInstance
    // when validation is enabled. Failure is non-fatal — we log a warning.
    void SetupDebugMessenger();
};

} // namespace Arcbit
