#include <arcbit/render/RenderDevice.h>
#include "VulkanContext.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Helpers (local to this translation unit)
// ---------------------------------------------------------------------------
namespace {

// Pick the best surface format. We prefer BGRA8_SRGB (the typical Windows
// swapchain format). If unavailable, fall back to the first format the driver
// reports — there will always be at least one.
VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());

    for (const auto& fmt : formats)
    {
        if (fmt.format     == VK_FORMAT_B8G8R8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return fmt;
    }
    return formats[0];
}

// Pick a present mode. With VSync enabled we use FIFO (guaranteed to be available).
// Without VSync we prefer MAILBOX (renders as fast as possible, replaces queued
// frames rather than tearing). Falls back to FIFO if MAILBOX is absent.
VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsync)
{
    if (vsync) return VK_PRESENT_MODE_FIFO_KHR;

    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());

    for (auto mode : modes)
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;

    return VK_PRESENT_MODE_FIFO_KHR;
}

// Determine the swapchain image size from the surface capabilities.
// When currentExtent is set to UINT32_MAX the surface has no fixed size and
// we clamp the requested dimensions to the allowed min/max range.
VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, u32 width, u32 height)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    return {
        std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

// Convert engine LoadOp → VkAttachmentLoadOp.
VkAttachmentLoadOp ToVkLoadOp(LoadOp op)
{
    switch (op)
    {
        case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

// Convert our Format enum → VkFormat.
VkFormat ToVkFormat(Format fmt)
{
    switch (fmt)
    {
        case Format::RGBA8_UNorm:       return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNorm:       return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RGBA16_Float:      return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32_Float:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32_Float:         return VK_FORMAT_R32_SFLOAT;
        case Format::D32_Float:         return VK_FORMAT_D32_SFLOAT;
        case Format::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
        default:                        return VK_FORMAT_UNDEFINED;
    }
}

// Convert VkFormat → our Format enum (used when querying swapchain format).
Format VkFormatToFormat(VkFormat fmt)
{
    switch (fmt)
    {
        case VK_FORMAT_R8G8B8A8_UNORM:        return Format::RGBA8_UNorm;
        case VK_FORMAT_R8G8B8A8_SRGB:         return Format::RGBA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:        return Format::BGRA8_UNorm;
        case VK_FORMAT_B8G8R8A8_SRGB:         return Format::BGRA8_SRGB;
        case VK_FORMAT_R16G16B16A16_SFLOAT:   return Format::RGBA16_Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT:   return Format::RGBA32_Float;
        case VK_FORMAT_R32_SFLOAT:            return Format::R32_Float;
        case VK_FORMAT_D32_SFLOAT:            return Format::D32_Float;
        case VK_FORMAT_D24_UNORM_S8_UINT:     return Format::D24_UNorm_S8_UInt;
        default:                              return Format::Undefined;
    }
}

VkCullModeFlags ToVkCullMode(CullMode mode)
{
    switch (mode)
    {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp ToVkCompareOp(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:        return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:         return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

VkBlendFactor ToVkBlendFactor(BlendFactor f)
{
    switch (f)
    {
        case BlendFactor::Zero:              return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:               return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:          return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:          return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:  return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:          return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:          return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ZERO;
}

VkBlendOp ToVkBlendOp(BlendOp op)
{
    switch (op)
    {
        case BlendOp::Add:             return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:             return VK_BLEND_OP_MIN;
        case BlendOp::Max:             return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkSamplerAddressMode ToVkAddressMode(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirrorRepeat:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

// Convert engine StoreOp → VkAttachmentStoreOp.
VkAttachmentStoreOp ToVkStoreOp(StoreOp op)
{
    switch (op)
    {
        case StoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

// Build (or rebuild) VkSwapchainKHR and all its images / views.
// Used by both CreateSwapchain and ResizeSwapchain.
// On success, fills sc->Swapchain, sc->Images, sc->ImageViews, sc->Extent.
// Returns false on any Vulkan error.
bool BuildSwapchain(VulkanContext* ctx, VulkanSwapchain* sc,
                    u32 width, u32 height, VkSwapchainKHR oldSwapchain)
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->PhysicalDevice, sc->Surface, &caps);

    const VkSurfaceFormatKHR fmt  = ChooseSurfaceFormat(ctx->PhysicalDevice, sc->Surface);
    const VkPresentModeKHR   mode = ChoosePresentMode(ctx->PhysicalDevice, sc->Surface, sc->VSync);
    const VkExtent2D         ext  = ChooseExtent(caps, width, height);

    // Request one more image than the minimum to avoid stalling on the driver.
    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface          = sc->Surface;
    info.minImageCount    = imageCount;
    info.imageFormat      = fmt.format;
    info.imageColorSpace  = fmt.colorSpace;
    info.imageExtent      = ext;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // TRANSFER_SRC lets CopySwapchainImage read a frame back (screenshots,
    // save thumbnails). Nearly universal, but optional per the spec - without
    // it a readback copies nothing (logged once).
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sc->CanCopyFrom = (info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    info.preTransform     = caps.currentTransform; // no extra rotation/flip
    info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // no window transparency
    info.presentMode      = mode;
    info.clipped          = VK_TRUE;    // don't render pixels obscured by other windows
    info.oldSwapchain     = oldSwapchain; // lets the driver reuse resources from the old swapchain

    // If graphics and present are different queue families the images need to
    // be accessible from both; otherwise exclusive mode is faster.
    u32 families[] = { ctx->GraphicsFamily, ctx->PresentFamily };
    if (ctx->GraphicsFamily != ctx->PresentFamily)
    {
        info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices   = families;
    }
    else
    {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult result = vkCreateSwapchainKHR(ctx->Device, &info, nullptr, &sc->Swapchain);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateSwapchainKHR failed ({})", static_cast<i32>(result));
        return false;
    }

    sc->Format = fmt.format;
    sc->Extent = ext;

    // Retrieve the driver-created swapchain images.
    u32 actualCount = 0;
    vkGetSwapchainImagesKHR(ctx->Device, sc->Swapchain, &actualCount, nullptr);
    sc->Images.resize(actualCount);
    vkGetSwapchainImagesKHR(ctx->Device, sc->Swapchain, &actualCount, sc->Images.data());

    // Create an image view for each swapchain image so we can use them as
    // color attachments in vkCmdBeginRendering.
    sc->ImageViews.resize(actualCount);
    for (u32 i = 0; i < actualCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image    = sc->Images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = fmt.format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        result = vkCreateImageView(ctx->Device, &viewInfo, nullptr, &sc->ImageViews[i]);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Render, "vkCreateImageView (swapchain image {}) failed", i);
            return false;
        }
    }

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RenderDevice — construction / destruction
// ---------------------------------------------------------------------------

// Takes ownership of the fully-initialized VulkanContext via unique_ptr.
// Construction is only reachable from Arcbit_CreateDevice because VulkanContext
// is an incomplete type in all headers outside this backend DLL.
RenderDevice::RenderDevice(std::unique_ptr<VulkanContext> context)
    : _context(std::move(context))
{}

// Destructor delegates to VulkanContext::Shutdown which destroys all Vulkan
// objects in reverse-init order. Callers should call WaitIdle() first to
// ensure no GPU work is still in flight.
RenderDevice::~RenderDevice()
{
    if (_context)
        _context->Shutdown();
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

// Allocates a GPU buffer and registers it in the handle pool.
//
// Memory strategy:
//   HostVisible = true  → VMA_MEMORY_USAGE_CPU_TO_GPU
//     Allocated in host-visible memory (often write-combined system RAM on desktop).
//     VMA maps it immediately and keeps it mapped persistently (VMA_ALLOCATION_CREATE_MAPPED_BIT).
//     CPU writes go directly to the buffer — no staging required.
//     Used for: per-frame uniform buffers, dynamic SSBO light lists.
//
//   HostVisible = false → VMA_MEMORY_USAGE_GPU_ONLY
//     Allocated in device-local VRAM — the fastest memory for GPU reads.
//     CPU cannot write to it directly; must use a staging buffer (see UpdateBuffer).
//     Used for: static vertex buffers, index buffers, read-only textures.
//
// Debug naming:
//   If desc.DebugName is set and the validation extension is present, the VkBuffer
//   gets a human-readable name that appears in RenderDoc, Nsight, and validation messages.
BufferHandle RenderDevice::CreateBuffer(const BufferDesc& desc)
{
    ARCBIT_ASSERT(desc.Size > 0, "Buffer size must be > 0");

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size        = desc.Size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (HasFlag(desc.Usage, BufferUsage::Vertex))   bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Index))    bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Uniform))  bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Storage))  bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.Usage, BufferUsage::Transfer)) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                                                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    // A readback buffer is the destination of an image-to-buffer copy.
    if (HasFlag(desc.Usage, BufferUsage::Readback)) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // GPU_TO_CPU picks host-visible memory the CPU can read quickly (cached
    // where available); CPU_TO_GPU memory is often write-combined, which is
    // fast to write but very slow to read back.
    VmaAllocationCreateInfo allocInfo{};
    if (!desc.HostVisible) allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    else if (HasFlag(desc.Usage, BufferUsage::Readback)) allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    else allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (desc.HostVisible)
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VulkanBuffer vkBuffer{};
    vkBuffer.Size        = desc.Size;
    vkBuffer.Usage       = desc.Usage;
    vkBuffer.HostVisible = desc.HostVisible;

    VmaAllocationInfo allocResult{};
    const VkResult result = vmaCreateBuffer(
        _context->Allocator,
        &bufferInfo, &allocInfo,
        &vkBuffer.Buffer, &vkBuffer.Allocation,
        &allocResult);

    ARCBIT_VERIFY(result == VK_SUCCESS, "vmaCreateBuffer failed");

    if (desc.HostVisible)
        vkBuffer.Mapped = allocResult.pMappedData;

    // Storage buffers need one descriptor set per frame-in-flight slot so the
    // render thread can cycle through them without racing the GPU.
    if (HasFlag(desc.Usage, BufferUsage::Storage))
    {
        std::array<VkDescriptorSetLayout, MaxFramesInFlight> layouts;
        layouts.fill(_context->StorageBufferSetLayout);

        VkDescriptorSetAllocateInfo setAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        setAlloc.descriptorPool     = _context->GlobalDescriptorPool;
        setAlloc.descriptorSetCount = MaxFramesInFlight;
        setAlloc.pSetLayouts        = layouts.data();

        const VkResult setResult = vkAllocateDescriptorSets(
            _context->Device, &setAlloc, vkBuffer.DescriptorSets.data());
        ARCBIT_VERIFY(setResult == VK_SUCCESS, "vkAllocateDescriptorSets (SSBO) failed");
    }

    if (desc.DebugName)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType   = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<u64>(vkBuffer.Buffer);
        nameInfo.pObjectName  = desc.DebugName;

        auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(_context->Device, "vkSetDebugUtilsObjectNameEXT"));
        if (fn) fn(_context->Device, &nameInfo);
    }

    return _context->Buffers.Allocate<BufferTag>(std::move(vkBuffer));
}

// Reads GPU-written data out of a host-visible buffer.
//
// VMA keeps the buffer persistently mapped. On memory that isn't
// HOST_COHERENT the CPU may still see stale cache lines, so the range is
// invalidated first (a no-op on coherent memory).
void RenderDevice::ReadBuffer(BufferHandle handle, void* out, u64 size, u64 offset)
{
    VulkanBuffer* buf = _context->Buffers.Get(handle);
    ARCBIT_ASSERT(buf != nullptr && buf->HostVisible, "ReadBuffer: invalid or not host-visible buffer");
    ARCBIT_ASSERT(offset + size <= buf->Size, "ReadBuffer: read out of bounds");
    vmaInvalidateAllocation(_context->Allocator, buf->Allocation, offset, size);
    memcpy(out, static_cast<const u8*>(buf->Mapped) + offset, size);
}

// Writes CPU data into a previously allocated buffer.
//
// Two paths depending on memory type:
//   Host-visible: memcpy directly into the persistently-mapped pointer. Fast.
//   Device-local: create a staging buffer, memcpy into it, issue a GPU copy
//                 command, wait, then destroy the staging buffer. Slower but
//                 device-local VRAM is faster for GPU shader reads.
void RenderDevice::UpdateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset)
{
    VulkanBuffer* buf = _context->Buffers.Get(handle);
    ARCBIT_ASSERT(buf != nullptr, "UpdateBuffer: invalid handle");
    ARCBIT_ASSERT(offset + size <= buf->Size, "UpdateBuffer: write out of bounds");

    if (buf->HostVisible)
    {
        memcpy(static_cast<u8*>(buf->Mapped) + offset, data, size);
    }
    else
    {
        // Create temporary staging buffer in host-visible memory.
        BufferDesc stagingDesc{};
        stagingDesc.Size        = size;
        stagingDesc.Usage       = BufferUsage::Transfer;
        stagingDesc.HostVisible = true;
        stagingDesc.DebugName   = "staging";

        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        VulkanBuffer* staging = _context->Buffers.Get(stagingHandle);
        memcpy(staging->Mapped, data, size);

        // Record a one-shot copy command and submit it immediately.
        VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAlloc.commandPool        = _context->CommandPool;
        cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(_context->Device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy region{ 0, offset, size };
        vkCmdCopyBuffer(cmd, staging->Buffer, buf->Buffer, 1, &region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(_context->GraphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(_context->GraphicsQueue);

        vkFreeCommandBuffers(_context->Device, _context->CommandPool, 1, &cmd);
        DestroyBuffer(stagingHandle);
    }
}

// Releases the GPU buffer. The handle is invalidated (generation bumped) so
// any remaining copies of it are detected as stale on next use.
void RenderDevice::DestroyBuffer(BufferHandle handle)
{
    auto resource = _context->Buffers.Free(handle);
    if (!resource) return;

    // Free any SSBO descriptor sets before the pool is reset.
    std::vector<VkDescriptorSet> setsToFree;
    for (const auto& ds : resource->DescriptorSets)
        if (ds != VK_NULL_HANDLE) setsToFree.push_back(ds);
    if (!setsToFree.empty())
    {
        vkFreeDescriptorSets(_context->Device, _context->GlobalDescriptorPool,
            static_cast<u32>(setsToFree.size()), setsToFree.data());
    }

    if (resource->Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(_context->Allocator, resource->Buffer, resource->Allocation);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

// Allocates a GPU image (VkImage + VkImageView via VMA) and, if the texture
// will be sampled in shaders, pre-allocates a VkDescriptorSet for it from the
// global pool. The descriptor is populated later by UploadTexture / BindTexture.
TextureHandle RenderDevice::CreateTexture(const TextureDesc& desc)
{
    ARCBIT_ASSERT(desc.Width > 0 && desc.Height > 0, "Texture dimensions must be > 0");

    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType   = VK_IMAGE_TYPE_2D;
    imageInfo.format      = ToVkFormat(desc.Format);
    imageInfo.extent      = { desc.Width, desc.Height, 1 };
    imageInfo.mipLevels   = desc.MipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL; // GPU-optimal layout; CPU cannot read

    if (HasFlag(desc.Usage, TextureUsage::Sampled))      imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(desc.Usage, TextureUsage::RenderTarget))  imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(desc.Usage, TextureUsage::DepthStencil)) imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(desc.Usage, TextureUsage::Storage))       imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(desc.Usage, TextureUsage::Transfer))      imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                                                           | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Sampled textures need TRANSFER_DST so UploadTexture can copy into them.
    if (HasFlag(desc.Usage, TextureUsage::Sampled))
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VulkanTexture vkTex{};
    vkTex.Format = imageInfo.format;
    vkTex.Width  = desc.Width;
    vkTex.Height = desc.Height;
    vkTex.Usage  = desc.Usage;

    const VkResult imgResult = vmaCreateImage(
        _context->Allocator,
        &imageInfo, &allocInfo,
        &vkTex.Image, &vkTex.Allocation, nullptr);
    ARCBIT_VERIFY(imgResult == VK_SUCCESS, "vmaCreateImage failed");

    // Determine the correct aspect flag (color vs depth).
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (HasFlag(desc.Usage, TextureUsage::DepthStencil))
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image    = vkTex.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = imageInfo.format;
    viewInfo.subresourceRange = { aspect, 0, desc.MipLevels, 0, 1 };

    const VkResult viewResult = vkCreateImageView(_context->Device, &viewInfo, nullptr, &vkTex.View);
    ARCBIT_VERIFY(viewResult == VK_SUCCESS, "vkCreateImageView failed");

    // Descriptor sets are created lazily, one per sampler the texture is
    // drawn with (see VulkanTexture::SamplerSets and BindTexture).
    vkTex.Sampled = HasFlag(desc.Usage, TextureUsage::Sampled);

    return _context->Textures.Allocate<TextureTag>(std::move(vkTex));
}

// Upload raw pixel data into a texture via a staging buffer.
//
// Transition sequence (sync2):
//   1. UNDEFINED → TRANSFER_DST_OPTIMAL  — discard old contents, prepare for copy.
//   2. Buffer-to-image copy.
//   3. TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL — ready for shader sampling.
//
// A one-shot command buffer is used and the queue is drained before returning,
// so the staging buffer can be freed immediately.
void RenderDevice::UploadTexture(TextureHandle handle, const void* data, u64 size)
{
    VulkanTexture* tex = _context->Textures.Get(handle);
    ARCBIT_ASSERT(tex != nullptr, "UploadTexture: invalid handle");

    // Staging buffer — host-visible, CPU writes → GPU reads.
    BufferDesc stagingDesc{};
    stagingDesc.Size        = size;
    stagingDesc.Usage       = BufferUsage::Transfer;
    stagingDesc.HostVisible = true;
    stagingDesc.DebugName   = "tex_staging";

    BufferHandle stagingHandle = CreateBuffer(stagingDesc);
    VulkanBuffer* staging = _context->Buffers.Get(stagingHandle);
    memcpy(staging->Mapped, data, size);

    // One-shot command buffer.
    VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool        = _context->CommandPool;
    cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(_context->Device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Barrier 1: UNDEFINED → TRANSFER_DST_OPTIMAL.
    // srcStage TOP_OF_PIPE + srcAccess NONE means "wait for nothing" — correct
    // because oldLayout = UNDEFINED discards existing image contents anyway.
    VkImageMemoryBarrier2 toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toTransfer.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_2_NONE;
    toTransfer.dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image         = tex->Image;
    toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep1{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep1.imageMemoryBarrierCount = 1;
    dep1.pImageMemoryBarriers    = &toTransfer;
    vkCmdPipelineBarrier2(cmd, &dep1);

    // Copy staging buffer → image (row-major, tightly packed).
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { tex->Width, tex->Height, 1 };
    vkCmdCopyBufferToImage(cmd, staging->Buffer, tex->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Barrier 2: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
    // Fragment shaders can now sample this texture.
    VkImageMemoryBarrier2 toShaderRead{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toShaderRead.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    toShaderRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toShaderRead.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toShaderRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.image         = tex->Image;
    toShaderRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep2{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep2.imageMemoryBarrierCount = 1;
    dep2.pImageMemoryBarriers    = &toShaderRead;
    vkCmdPipelineBarrier2(cmd, &dep2);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(_context->GraphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(_context->GraphicsQueue);

    vkFreeCommandBuffers(_context->Device, _context->CommandPool, 1, &cmd);
    DestroyBuffer(stagingHandle);
}

// Release the texture and its GPU memory.
// Swapchain images (IsSwapchainImage = true) are driver-owned — we only
// destroy our VkImageView for them, not the underlying VkImage.
void RenderDevice::DestroyTexture(TextureHandle handle)
{
    auto resource = _context->Textures.Free(handle);
    if (!resource) return;

    // Return every per-sampler descriptor set to the pool before destroying
    // the image they point at.
    for (const VulkanTexture::SamplerSet& entry : resource->SamplerSets)
        vkFreeDescriptorSets(_context->Device, _context->GlobalDescriptorPool, 1, &entry.Set);

    if (resource->View != VK_NULL_HANDLE)
        vkDestroyImageView(_context->Device, resource->View, nullptr);

    if (!resource->IsSwapchainImage && resource->Image != VK_NULL_HANDLE)
        vmaDestroyImage(_context->Allocator, resource->Image, resource->Allocation);
}

// ---------------------------------------------------------------------------
// Samplers
// ---------------------------------------------------------------------------

// Creates a VkSampler from the given filtering and addressing parameters.
// Samplers are cheap to create and can be shared across many textures.
SamplerHandle RenderDevice::CreateSampler(const SamplerDesc& desc)
{
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter    = (desc.MagFilter == Filter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter    = (desc.MinFilter == Filter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.addressModeU = ToVkAddressMode(desc.AddressU);
    info.addressModeV = ToVkAddressMode(desc.AddressV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    if (desc.Anisotropy && _context->PhysicalDeviceFeats.samplerAnisotropy)
    {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy    = std::min(desc.MaxAniso,
            _context->PhysicalDeviceProps.limits.maxSamplerAnisotropy);
    }

    // Standard range for normalized UV coordinates.
    info.minLod          = 0.0f;
    info.maxLod          = VK_LOD_CLAMP_NONE;
    info.borderColor     = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;

    VulkanSampler vkSampler{};
    const VkResult result = vkCreateSampler(_context->Device, &info, nullptr, &vkSampler.Sampler);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreateSampler failed");

    return _context->Samplers.Allocate<SamplerTag>(std::move(vkSampler));
}

void RenderDevice::DestroySampler(SamplerHandle handle)
{
    auto resource = _context->Samplers.Free(handle);
    if (!resource) return;
    if (resource->Sampler == VK_NULL_HANDLE) return;

    // Free every texture's descriptor set that was written with this sampler.
    // The driver may hand the same VkSampler value to a sampler created
    // later; BindTexture would then find the old set - which references the
    // destroyed sampler - and bind it. Like all Destroy* calls, this runs
    // while the render thread is idle, so no command buffer holds these sets.
    _context->Textures.ForEach([&](VulkanTexture& tex) {
        std::erase_if(tex.SamplerSets, [&](const VulkanTexture::SamplerSet& entry) {
            if (entry.Sampler != resource->Sampler) return false;
            vkFreeDescriptorSets(_context->Device, _context->GlobalDescriptorPool, 1, &entry.Set);
            return true;
        });
    });
    vkDestroySampler(_context->Device, resource->Sampler, nullptr);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Compiles a SPIR-V shader module on the device.
// The bytecode is copied into driver memory — the caller's array can be freed
// after this returns. SPIR-V requires 4-byte alignment on size and pointer.
ShaderHandle RenderDevice::CreateShader(const ShaderDesc& desc)
{
    ARCBIT_ASSERT(desc.Code != nullptr && desc.CodeSize > 0, "Shader code must not be empty");
    ARCBIT_ASSERT(desc.CodeSize % 4 == 0, "SPIR-V bytecode must be 4-byte aligned");

    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = desc.CodeSize;
    info.pCode    = reinterpret_cast<const u32*>(desc.Code);

    VulkanShader vkShader{};
    vkShader.Stage = desc.Stage;

    const VkResult result = vkCreateShaderModule(_context->Device, &info, nullptr, &vkShader.Module);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreateShaderModule failed");

    return _context->Shaders.Allocate<ShaderTag>(std::move(vkShader));
}

// Destroys the shader module. Pipelines built from this shader are unaffected
// because Vulkan copies the SPIR-V during pipeline creation.
void RenderDevice::DestroyShader(ShaderHandle handle)
{
    auto resource = _context->Shaders.Free(handle);
    if (!resource) return;
    if (resource->Module != VK_NULL_HANDLE)
        vkDestroyShaderModule(_context->Device, resource->Module, nullptr);
}

// ---------------------------------------------------------------------------
// Swapchain color format query
// ---------------------------------------------------------------------------

// Returns the color format the swapchain was actually created with.
// Use this when creating a pipeline so PipelineDesc::ColorFormat matches.
Format RenderDevice::GetSwapchainColorFormat(SwapchainHandle handle)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc != nullptr, "GetSwapchainColorFormat: invalid handle");
    return VkFormatToFormat(sc->Format);
}

// Returns the actual pixel extent of the swapchain after any resize.
// The render thread calls this after AcquireNextImage to get the true
// dimensions rather than relying on the potentially-stale packet values.
void RenderDevice::GetSwapchainExtent(SwapchainHandle handle, u32& outWidth, u32& outHeight)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc != nullptr, "GetSwapchainExtent: invalid handle");
    outWidth  = sc->Extent.width;
    outHeight = sc->Extent.height;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

// Creates a graphics pipeline — shaders, vertex input, rasterizer, depth,
// blend, and dynamic state (viewport + scissor set at draw time).
//
// Uses VkPipelineRenderingCreateInfo (dynamic rendering, Vulkan 1.3) in the
// pNext chain instead of a VkRenderPass. ColorFormat / DepthFormat must match
// what is passed to BeginRendering each frame.
//
// Pipeline creation is expensive — call at load time, not per-frame.
PipelineHandle RenderDevice::CreatePipeline(const PipelineDesc& desc)
{
    VulkanShader* vertShader = _context->Shaders.Get(desc.VertexShader);
    VulkanShader* fragShader = _context->Shaders.Get(desc.FragmentShader);
    ARCBIT_ASSERT(vertShader && fragShader, "CreatePipeline: invalid shader handles");

    // Shader stages — one struct per stage.
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShader->Module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShader->Module;
    stages[1].pName  = "main";

    // Vertex input — translate engine binding/attribute descriptions to Vulkan.
    // Empty arrays are valid (e.g. hardcoded geometry in the vertex shader).
    std::vector<VkVertexInputBindingDescription>   bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    for (const auto& b : desc.Bindings)
    {
        VkVertexInputBindingDescription bd{};
        bd.binding   = b.Binding;
        bd.stride    = b.Stride;
        bd.inputRate = b.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(bd);
    }
    for (const auto& a : desc.Attributes)
    {
        VkVertexInputAttributeDescription ad{};
        ad.location = a.Location;
        ad.binding  = a.Binding;
        ad.format   = ToVkFormat(a.Format);
        ad.offset   = a.Offset;
        attributes.push_back(ad);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount   = static_cast<u32>(bindings.size());
    vertexInput.pVertexBindingDescriptions      = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic — set per-draw via SetViewport / SetScissor.
    // We still tell Vulkan how many there are (1 each).
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = desc.Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = ToVkCullMode(desc.CullMode);
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    // No multisampling for now — single sample.
    VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable  = desc.Depth.TestEnable  ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.Depth.WriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp   = ToVkCompareOp(desc.Depth.Compare);

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                               | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.Blend.Enable)
    {
        blendAttach.blendEnable         = VK_TRUE;
        blendAttach.srcColorBlendFactor = ToVkBlendFactor(desc.Blend.SrcColor);
        blendAttach.dstColorBlendFactor = ToVkBlendFactor(desc.Blend.DstColor);
        blendAttach.colorBlendOp        = ToVkBlendOp(desc.Blend.ColorOp);
        blendAttach.srcAlphaBlendFactor = ToVkBlendFactor(desc.Blend.SrcAlpha);
        blendAttach.dstAlphaBlendFactor = ToVkBlendFactor(desc.Blend.DstAlpha);
        blendAttach.alphaBlendOp        = ToVkBlendOp(desc.Blend.AlphaOp);
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    // Pipeline layout. Declare a 128-byte push constant block covering all stages
    // so any shader can use push constants without needing to recreate the layout.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 128;

    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    // Build the descriptor set layout list based on the pipeline's feature flags.
    // The ordering must match what the shader expects:
    //   set 0 — albedo texture      (UseTextures)
    //   set 1 — normal map texture  (UseNormalTexture; requires UseTextures)
    //   set 2 — storage buffer SSBO (UseStorageBuffer; appended after texture sets)
    //   set 3 — shadow map SSBO     (UseShadowBuffer; requires UseStorageBuffer)
    std::vector<VkDescriptorSetLayout> setLayouts;
    if (desc.UseTextures)      setLayouts.push_back(_context->TextureSetLayout);
    if (desc.UseNormalTexture) setLayouts.push_back(_context->TextureSetLayout);
    if (desc.UseStorageBuffer) setLayouts.push_back(_context->StorageBufferSetLayout);
    if (desc.UseShadowBuffer)  setLayouts.push_back(_context->StorageBufferSetLayout);

    if (!setLayouts.empty())
    {
        layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
        layoutInfo.pSetLayouts    = setLayouts.data();
    }

    VulkanPipeline vkPipeline{};
    VkResult result = vkCreatePipelineLayout(_context->Device, &layoutInfo, nullptr, &vkPipeline.Layout);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreatePipelineLayout failed");

    // Dynamic rendering: describe the attachment formats without a VkRenderPass.
    // These must match what's passed to vkCmdBeginRendering each frame.
    VkFormat colorFmt = ToVkFormat(desc.ColorFormat);
    VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &colorFmt;
    if (desc.DepthFormat != Format::Undefined)
        renderingInfo.depthAttachmentFormat = ToVkFormat(desc.DepthFormat);

    VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.pNext               = &renderingInfo; // dynamic rendering — no VkRenderPass
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = vkPipeline.Layout;
    // renderPass = VK_NULL_HANDLE (dynamic rendering handles attachment setup)

    result = vkCreateGraphicsPipelines(_context->Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline.Pipeline);
    ARCBIT_VERIFY(result == VK_SUCCESS, "vkCreateGraphicsPipelines failed");

    if (desc.DebugName)
    {
        auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(_context->Device, "vkSetDebugUtilsObjectNameEXT"));
        if (fn)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType   = VK_OBJECT_TYPE_PIPELINE;
            nameInfo.objectHandle = reinterpret_cast<u64>(vkPipeline.Pipeline);
            nameInfo.pObjectName  = desc.DebugName;
            fn(_context->Device, &nameInfo);
        }
    }

    LOG_DEBUG(Render, "Pipeline created{}", desc.DebugName ? std::string(" (") + desc.DebugName + ")" : "");
    return _context->Pipelines.Allocate<PipelineTag>(std::move(vkPipeline));
}

void RenderDevice::DestroyPipeline(PipelineHandle handle)
{
    auto resource = _context->Pipelines.Free(handle);
    if (!resource) return;
    if (resource->Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(_context->Device, resource->Pipeline, nullptr);
    if (resource->Layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(_context->Device, resource->Layout, nullptr);
}

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------

// Creates a swapchain for window presentation.
//
// The VkSurfaceKHR was created during VulkanContext::Init (needed early for
// physical device selection). We take ownership of it here via m_InitSurface
// so it is properly destroyed with the swapchain.
//
// Per-frame sync objects (one set per in-flight frame):
//   ImageAvailable — signalled when vkAcquireNextImageKHR gives us an image.
//   RenderFinished — signalled by Submit; vkQueuePresentKHR waits on it.
//   InFlight       — CPU fence; we wait on it in AcquireNextImage before
//                    reusing that frame's command buffer and resources.
SwapchainHandle RenderDevice::CreateSwapchain(const SwapchainDesc& desc)
{
    // Transfer the surface we created during Init into this swapchain.
    ARCBIT_ASSERT(_context->m_InitSurface != VK_NULL_HANDLE,
        "CreateSwapchain: no surface available (Init surface already consumed)");

    VulkanSwapchain sc{};
    sc.Surface = _context->m_InitSurface;
    sc.VSync   = desc.VSync;
    _context->m_InitSurface = VK_NULL_HANDLE; // swapchain now owns it

    if (!BuildSwapchain(_context.get(), &sc, desc.Width, desc.Height, VK_NULL_HANDLE))
        return SwapchainHandle::Invalid();

    // Register each swapchain image in the texture pool so the rest of the API
    // can refer to them by TextureHandle (e.g. as a color attachment).
    const u32 imageCount = static_cast<u32>(sc.Images.size());
    sc.ImageHandles.resize(imageCount);
    for (u32 i = 0; i < imageCount; ++i)
    {
        VulkanTexture vkTex{};
        vkTex.Image            = sc.Images[i];
        vkTex.View             = sc.ImageViews[i];
        vkTex.Allocation       = VK_NULL_HANDLE; // driver-owned — not a VMA allocation
        vkTex.Format           = sc.Format;
        vkTex.Width            = sc.Extent.width;
        vkTex.Height           = sc.Extent.height;
        vkTex.Usage            = TextureUsage::RenderTarget;
        vkTex.IsSwapchainImage = true;
        sc.ImageHandles[i]     = _context->Textures.Allocate<TextureTag>(std::move(vkTex));
    }

    // ImageAvailable — one per frame-in-flight slot (indexed by CurrentFrame).
    //   Safe because the InFlight fence guarantees the submit that consumed it has
    //   completed before we reuse the slot.
    //
    // RenderFinished — one per swapchain image (indexed by CurrentImageIndex).
    //   Must NOT be per-frame-slot: the present engine holds the semaphore until
    //   the image is actually shown, which can outlive the InFlight fence. Since
    //   the swapchain cannot re-issue image N until its previous present completes,
    //   RenderFinished[N] is always free when image N is next acquired.
    //
    // InFlight — one per frame-in-flight slot. CPU fence; guards command buffer reuse.
    sc.ImageAvailable.resize(MaxFramesInFlight);
    sc.RenderFinished.resize(imageCount);  // one per swapchain image, not per frame slot
    sc.InFlight.resize(MaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    // Start fences in the signalled state so the first frame doesn't wait forever.
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < MaxFramesInFlight; ++i)
    {
        vkCreateSemaphore(_context->Device, &semInfo,   nullptr, &sc.ImageAvailable[i]);
        vkCreateFence    (_context->Device, &fenceInfo, nullptr, &sc.InFlight[i]);
    }
    for (u32 i = 0; i < imageCount; ++i)
    {
        vkCreateSemaphore(_context->Device, &semInfo, nullptr, &sc.RenderFinished[i]);
    }

    // Pre-allocate the per-frame command buffers the first time a swapchain is created.
    if (_context->FrameCommandBuffers[0] == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAlloc.commandPool        = _context->CommandPool;
        cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = MaxFramesInFlight;
        vkAllocateCommandBuffers(_context->Device, &cmdAlloc,
            _context->FrameCommandBuffers.data());
    }

    SwapchainHandle handle = _context->Swapchains.Allocate<SwapchainTag>(std::move(sc));
    LOG_INFO(Render, "Swapchain created ({}x{}, {} images)",
        desc.Width, desc.Height, imageCount);
    return handle;
}

// Recreates the swapchain at a new size.
// Must be called when the window is resized or vkAcquireNextImageKHR / Present
// returns VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR.
// All in-flight frames must be complete before calling.
void RenderDevice::ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc != nullptr, "ResizeSwapchain: invalid handle");

    vkDeviceWaitIdle(_context->Device);

    // Free the old texture handles (but not the image views — we'll destroy those next).
    for (auto texHandle : sc->ImageHandles)
        _context->Textures.Free(texHandle);
    sc->ImageHandles.clear();

    // Destroy old image views.
    for (auto view : sc->ImageViews)
        vkDestroyImageView(_context->Device, view, nullptr);
    sc->ImageViews.clear();
    sc->Images.clear();

    // Pass the old swapchain handle to BuildSwapchain so the driver can reuse
    // its resources internally before we destroy it.
    VkSwapchainKHR oldSwapchain = sc->Swapchain;
    sc->Swapchain = VK_NULL_HANDLE;

    if (!BuildSwapchain(_context.get(), sc, width, height, oldSwapchain))
    {
        LOG_ERROR(Render, "ResizeSwapchain: failed to rebuild swapchain");
        vkDestroySwapchainKHR(_context->Device, oldSwapchain, nullptr);
        return;
    }

    vkDestroySwapchainKHR(_context->Device, oldSwapchain, nullptr);

    // Re-register images in the texture pool.
    const u32 newImageCount = static_cast<u32>(sc->Images.size());
    sc->ImageHandles.resize(newImageCount);
    for (u32 i = 0; i < newImageCount; ++i)
    {
        VulkanTexture vkTex{};
        vkTex.Image            = sc->Images[i];
        vkTex.View             = sc->ImageViews[i];
        vkTex.Format           = sc->Format;
        vkTex.Width            = sc->Extent.width;
        vkTex.Height           = sc->Extent.height;
        vkTex.Usage            = TextureUsage::RenderTarget;
        vkTex.IsSwapchainImage = true;
        sc->ImageHandles[i]    = _context->Textures.Allocate<TextureTag>(std::move(vkTex));
    }

    // Recreate RenderFinished semaphores sized to the new image count.
    for (auto sem : sc->RenderFinished)
        vkDestroySemaphore(_context->Device, sem, nullptr);
    sc->RenderFinished.resize(newImageCount);
    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (u32 i = 0; i < newImageCount; ++i)
        vkCreateSemaphore(_context->Device, &semInfo, nullptr, &sc->RenderFinished[i]);

    sc->CurrentFrame      = 0;
    sc->CurrentImageIndex = 0;

    LOG_INFO(Render, "Swapchain resized to {}x{}", width, height);
}

// Destroys the swapchain and all its resources.
void RenderDevice::DestroySwapchain(SwapchainHandle handle)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    if (!sc) return;

    vkDeviceWaitIdle(_context->Device);

    // ImageAvailable and InFlight are per-frame-slot (MaxFramesInFlight).
    for (u32 i = 0; i < static_cast<u32>(sc->ImageAvailable.size()); ++i)
    {
        vkDestroySemaphore(_context->Device, sc->ImageAvailable[i], nullptr);
        vkDestroyFence    (_context->Device, sc->InFlight[i],       nullptr);
    }
    // RenderFinished is per-swapchain-image (may differ from MaxFramesInFlight).
    for (auto sem : sc->RenderFinished)
        vkDestroySemaphore(_context->Device, sem, nullptr);

    // Free texture pool entries. The image views are owned by us; the images
    // are driver-owned and will be destroyed with the swapchain below.
    for (auto texHandle : sc->ImageHandles)
        _context->Textures.Free(texHandle);

    for (auto view : sc->ImageViews)
        vkDestroyImageView(_context->Device, view, nullptr);

    vkDestroySwapchainKHR(_context->Device, sc->Swapchain, nullptr);
    vkDestroySurfaceKHR  (_context->Instance, sc->Surface, nullptr);

    _context->Swapchains.Free(handle);

    LOG_INFO(Render, "Swapchain destroyed");
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

// Acquires the next available swapchain image.
//
// Blocks on the in-flight fence for this frame slot (ensuring the GPU is
// done with any previous use of these resources), then calls
// vkAcquireNextImageKHR which signals ImageAvailable when the image is ready.
//
// Returns TextureHandle::Invalid() if the swapchain is out of date and must
// be recreated (the caller should call ResizeSwapchain and retry).
TextureHandle RenderDevice::AcquireNextImage(SwapchainHandle handle)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc != nullptr, "AcquireNextImage: invalid handle");

    // Wait for the GPU to finish the previous use of this frame slot.
    vkWaitForFences(_context->Device, 1, &sc->InFlight[sc->CurrentFrame], VK_TRUE, UINT64_MAX);

    u32 imageIndex = 0;
    const VkResult result = vkAcquireNextImageKHR(
        _context->Device,
        sc->Swapchain,
        UINT64_MAX,
        sc->ImageAvailable[sc->CurrentFrame], // signalled when the image is actually ready
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Do NOT reset the fence here — we skipped Submit, so the fence is still
        // signaled from the previous frame. Leaving it signaled ensures the next
        // AcquireNextImage call for this slot returns from vkWaitForFences
        // immediately instead of hanging forever.
        LOG_WARN(Render, "AcquireNextImage: swapchain out of date - caller should resize");
        return TextureHandle::Invalid();
    }

    // Reset only after a successful acquire — vkQueueSubmit requires the fence
    // to be unsignaled, and we're about to reuse this slot's command buffer.
    vkResetFences(_context->Device, 1, &sc->InFlight[sc->CurrentFrame]);

    sc->CurrentImageIndex  = imageIndex;
    _context->ActiveSwapchain = handle; // Submit/Present pick this up

    return sc->ImageHandles[imageIndex];
}

// ---------------------------------------------------------------------------
// Command recording
// ---------------------------------------------------------------------------

// Allocate and begin a command buffer for this frame.
// Safe to call because AcquireNextImage waited on the in-flight fence,
// guaranteeing the GPU is finished with this frame's command buffer.
CommandListHandle RenderDevice::BeginCommandList()
{
    VulkanSwapchain* sc = _context->Swapchains.Get(_context->ActiveSwapchain);
    ARCBIT_ASSERT(sc != nullptr, "BeginCommandList: no active swapchain (call AcquireNextImage first)");

    VkCommandBuffer cmd = _context->FrameCommandBuffers[sc->CurrentFrame];

    // Reset so we can re-record into it this frame.
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VulkanCommandList cmdList{ cmd };
    return _context->CommandLists.Allocate<CommandListTag>(std::move(cmdList));
}

// Finish recording. The command list is ready for Submit after this.
void RenderDevice::EndCommandList(CommandListHandle handle)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(handle);
    ARCBIT_ASSERT(cmdList != nullptr, "EndCommandList: invalid handle");
    vkEndCommandBuffer(cmdList->Buffer);
}

// Begin a dynamic rendering pass (Vulkan 1.3 vkCmdBeginRendering).
//
// For each color attachment we first issue a synchronization2 image barrier to
// transition the layout to COLOR_ATTACHMENT_OPTIMAL.  Using oldLayout = UNDEFINED
// discards previous contents, which is correct when loading with Clear or DontCare.
// With LoadOp::Load the barrier starts from the image's tracked layout instead,
// so what an earlier pass drew this frame survives (a frame split around a
// CopySwapchainImage).
void RenderDevice::BeginRendering(CommandListHandle cmd, const RenderingDesc& desc)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList != nullptr, "BeginRendering: invalid command list");

    std::vector<VkImageMemoryBarrier2>     barriers;
    std::vector<VkRenderingAttachmentInfo> colorAttachInfos;

    VkExtent2D extent{};

    for (const auto& attach : desc.ColorAttachments)
    {
        VulkanTexture* tex = _context->Textures.Get(attach.Texture);
        ARCBIT_ASSERT(tex != nullptr, "BeginRendering: invalid color attachment texture");

        // Transition: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
        // srcStage TOP_OF_PIPE means "wait for nothing before transitioning",
        // which is correct because we declared oldLayout = UNDEFINED (contents discarded).
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image         = tex->Image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // Keeping the contents: transition from the layout the last command
        // left (PRESENT_SRC after EndRendering / CopySwapchainImage). Every
        // earlier command on the image - the copy reading it, the pass writing
        // it - must finish first, and the new pass both reads (blending) and
        // writes it.
        if (attach.Load == LoadOp::Load && tex->Layout != VK_IMAGE_LAYOUT_UNDEFINED)
        {
            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout     = tex->Layout;
        }
        barriers.push_back(barrier);
        tex->Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkRenderingAttachmentInfo attachInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        attachInfo.imageView   = tex->View;
        attachInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachInfo.loadOp      = ToVkLoadOp(attach.Load);
        attachInfo.storeOp     = ToVkStoreOp(attach.Store);
        attachInfo.clearValue.color = {
            attach.ClearColor[0], attach.ClearColor[1],
            attach.ClearColor[2], attach.ClearColor[3]
        };
        colorAttachInfos.push_back(attachInfo);

        extent = { tex->Width, tex->Height };
    }

    VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = static_cast<u32>(barriers.size());
    depInfo.pImageMemoryBarriers    = barriers.data();
    vkCmdPipelineBarrier2(cmdList->Buffer, &depInfo);

    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea            = { { 0, 0 }, extent };
    renderingInfo.layerCount            = 1;
    renderingInfo.colorAttachmentCount  = static_cast<u32>(colorAttachInfos.size());
    renderingInfo.pColorAttachments     = colorAttachInfos.data();

    vkCmdBeginRendering(cmdList->Buffer, &renderingInfo);
}

// End the rendering pass and transition the color attachments to the correct
// final layout. Swapchain images must be in PRESENT_SRC_KHR before vkQueuePresentKHR.
void RenderDevice::EndRendering(CommandListHandle cmd)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList != nullptr, "EndRendering: invalid command list");

    vkCmdEndRendering(cmdList->Buffer);

    // Transition the active swapchain image from COLOR_ATTACHMENT_OPTIMAL to
    // PRESENT_SRC_KHR so vkQueuePresentKHR can display it.
    VulkanSwapchain* sc = _context->Swapchains.Get(_context->ActiveSwapchain);
    if (!sc) return;

    VulkanTexture* tex = _context->Textures.Get(sc->ImageHandles[sc->CurrentImageIndex]);
    if (!tex) return;

    VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_NONE;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image         = tex->Image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmdList->Buffer, &depInfo);
    tex->Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

// Copy the swapchain image being rendered into a host-visible buffer.
//
// The image sits in PRESENT_SRC_KHR (EndRendering put it there). A copy needs
// TRANSFER_SRC_OPTIMAL, so: barrier into it - waiting for the pass's color
// writes to land - copy, then barrier back to PRESENT_SRC_KHR so the frame can
// still be presented (or rendered on with LoadOp::Load). The buffer is read on
// the CPU only after the frame's fence, so no host barrier is needed here.
void RenderDevice::CopySwapchainImage(CommandListHandle cmd, BufferHandle dst)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    VulkanBuffer*      buf     = _context->Buffers.Get(dst);
    VulkanSwapchain*   sc      = _context->Swapchains.Get(_context->ActiveSwapchain);
    ARCBIT_ASSERT(cmdList && buf && sc, "CopySwapchainImage: invalid command list, buffer or swapchain");
    if (!sc->CanCopyFrom)
    {
        static bool warned = false;
        if (!std::exchange(warned, true)) LOG_WARN(Render, "CopySwapchainImage: the surface doesn't allow copies - skipped");
        return;
    }

    VulkanTexture* tex = _context->Textures.Get(sc->ImageHandles[sc->CurrentImageIndex]);
    ARCBIT_ASSERT(tex != nullptr, "CopySwapchainImage: no current swapchain image");
    ARCBIT_ASSERT(buf->Size >= u64(tex->Width) * tex->Height * 4, "CopySwapchainImage: buffer too small");

    VkImageMemoryBarrier2 toCopy{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toCopy.srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toCopy.srcAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toCopy.dstStageMask     = VK_PIPELINE_STAGE_2_COPY_BIT;
    toCopy.dstAccessMask    = VK_ACCESS_2_TRANSFER_READ_BIT;
    toCopy.oldLayout        = tex->Layout;
    toCopy.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.image            = tex->Image;
    toCopy.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &toCopy;
    vkCmdPipelineBarrier2(cmdList->Buffer, &dep);

    // bufferRowLength / bufferImageHeight = 0: rows are tightly packed.
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { tex->Width, tex->Height, 1 };
    vkCmdCopyImageToBuffer(cmdList->Buffer, tex->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf->Buffer, 1, &region);

    VkImageMemoryBarrier2 back = toCopy;
    back.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    back.srcAccessMask = VK_ACCESS_2_NONE; // a read: only the execution order matters
    back.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    back.dstAccessMask = VK_ACCESS_2_NONE;
    back.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dep.pImageMemoryBarriers = &back;
    vkCmdPipelineBarrier2(cmdList->Buffer, &dep);
    tex->Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

void RenderDevice::BindPipeline(CommandListHandle cmd, PipelineHandle pipeline)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    VulkanPipeline*    pip     = _context->Pipelines.Get(pipeline);
    ARCBIT_ASSERT(cmdList && pip, "BindPipeline: invalid handle");
    vkCmdBindPipeline(cmdList->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pip->Pipeline);
    cmdList->BoundLayout = pip->Layout; // stored so PushConstants can use it
}

void RenderDevice::BindVertexBuffer(CommandListHandle cmd, BufferHandle buffer, u32 binding, u64 offset)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    VulkanBuffer*      buf     = _context->Buffers.Get(buffer);
    ARCBIT_ASSERT(cmdList && buf, "BindVertexBuffer: invalid handle");
    vkCmdBindVertexBuffers(cmdList->Buffer, binding, 1, &buf->Buffer, &offset);
}

void RenderDevice::BindIndexBuffer(CommandListHandle cmd, BufferHandle buffer, IndexType type, u64 offset)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    VulkanBuffer*      buf     = _context->Buffers.Get(buffer);
    ARCBIT_ASSERT(cmdList && buf, "BindIndexBuffer: invalid handle");
    const VkIndexType vkType = (type == IndexType::U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmdList->Buffer, buf->Buffer, offset, vkType);
}

// Find (or create) the texture's descriptor set for this sampler.
//
// The set is allocated from the global pool and written with the texture's
// image view and the sampler the first time this pair is drawn, then reused
// unchanged forever after - see VulkanTexture::SamplerSets for why it must
// never be rewritten. Allocation happens on the render thread while it records
// a frame; the pool is otherwise only touched by Create* / Destroy* calls,
// which run while the render thread is idle, so access never overlaps.
static VkDescriptorSet SamplerSetFor(VulkanContext& ctx, VulkanTexture& tex, const VkSampler sampler)
{
    for (const VulkanTexture::SamplerSet& entry : tex.SamplerSets)
        if (entry.Sampler == sampler) return entry.Set;

    VkDescriptorSetAllocateInfo setAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    setAlloc.descriptorPool     = ctx.GlobalDescriptorPool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts        = &ctx.TextureSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    ARCBIT_VERIFY(vkAllocateDescriptorSets(ctx.Device, &setAlloc, &set) == VK_SUCCESS,
                  "vkAllocateDescriptorSets (texture) failed - descriptor pool exhausted?");

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler;
    imageInfo.imageView   = tex.View;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(ctx.Device, 1, &write, 0, nullptr); // the only write this set ever gets

    tex.SamplerSets.push_back({ sampler, set });
    return set;
}

// Bind a texture + sampler pair to the pipeline at the given descriptor set
// slot. Each pair has its own descriptor set, written once on first use (see
// SamplerSetFor), so a texture may be bound with any mix of samplers, any
// number of times per frame, without ever updating a set the command buffer
// has already recorded.
void RenderDevice::BindTexture(CommandListHandle cmd, TextureHandle texture,
                                SamplerHandle sampler, u32 set, u32 /*binding*/)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    VulkanTexture*     tex     = _context->Textures.Get(texture);
    VulkanSampler*     samp    = _context->Samplers.Get(sampler);
    ARCBIT_ASSERT(cmdList && tex && samp, "BindTexture: invalid handle");
    ARCBIT_ASSERT(tex->Sampled, "BindTexture: texture was not created with Sampled usage");

    const VkDescriptorSet dstSet = SamplerSetFor(*_context, *tex, samp->Sampler);
    vkCmdBindDescriptorSets(cmdList->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        cmdList->BoundLayout, set, 1, &dstSet, 0, nullptr);
}

// Binds a storage buffer (SSBO) to the given descriptor set and binding.
// Updates the descriptor set for the current frame-in-flight slot, then
// calls vkCmdBindDescriptorSets so the shader can read it.
void RenderDevice::BindStorageBuffer(CommandListHandle cmd, BufferHandle buffer,
                                      u32 set, u32 binding)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList && cmdList->BoundLayout != VK_NULL_HANDLE,
        "BindStorageBuffer: no pipeline bound");

    VulkanBuffer* vkBuffer = _context->Buffers.Get(buffer);
    ARCBIT_ASSERT(vkBuffer, "BindStorageBuffer: invalid buffer handle");

    VulkanSwapchain* sc = _context->Swapchains.Get(_context->ActiveSwapchain);
    ARCBIT_ASSERT(sc, "BindStorageBuffer: no active swapchain");

    const u32 frame = sc->CurrentFrame;
    VkDescriptorSet dstSet = vkBuffer->DescriptorSets[frame];
    ARCBIT_ASSERT(dstSet != VK_NULL_HANDLE, "BindStorageBuffer: buffer has no SSBO descriptor set");

    // Only update the descriptor set if it hasn't been written yet for this frame.
    // Calling vkUpdateDescriptorSets on a set that's already recorded (bound) in
    // the current command buffer would put the command buffer into an invalid state.
    if (!vkBuffer->DescriptorSetWritten[frame])
    {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = vkBuffer->Buffer;
        bufInfo.offset = 0;
        bufInfo.range  = vkBuffer->Size;

        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet          = dstSet;
        write.dstBinding      = binding;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo     = &bufInfo;

        vkUpdateDescriptorSets(_context->Device, 1, &write, 0, nullptr);
        vkBuffer->DescriptorSetWritten[frame] = true;
    }

    vkCmdBindDescriptorSets(cmdList->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        cmdList->BoundLayout, set, 1, &dstSet, 0, nullptr);
}

u32 RenderDevice::GetCurrentFrameIndex(SwapchainHandle handle)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc, "GetCurrentFrameIndex: invalid swapchain handle");
    return sc->CurrentFrame;
}

void RenderDevice::PushConstants(CommandListHandle cmd, ShaderStage stages,
                                  const void* data, u32 size, u32 offset)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList && cmdList->BoundLayout != VK_NULL_HANDLE,
        "PushConstants: no pipeline bound");

    VkShaderStageFlags vkStages = 0;
    if (HasFlag(stages, ShaderStage::Vertex))   vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
    if (HasFlag(stages, ShaderStage::Fragment))  vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (HasFlag(stages, ShaderStage::Compute))   vkStages |= VK_SHADER_STAGE_COMPUTE_BIT;

    vkCmdPushConstants(cmdList->Buffer, cmdList->BoundLayout, vkStages, offset, size, data);
}

void RenderDevice::SetViewport(CommandListHandle cmd, f32 x, f32 y,
                                f32 width, f32 height, f32 minDepth, f32 maxDepth)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList, "SetViewport: invalid command list");
    VkViewport vp{ x, y, width, height, minDepth, maxDepth };
    vkCmdSetViewport(cmdList->Buffer, 0, 1, &vp);
}

void RenderDevice::SetScissor(CommandListHandle cmd, i32 x, i32 y, u32 width, u32 height)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList, "SetScissor: invalid command list");
    VkRect2D scissor{ { x, y }, { width, height } };
    vkCmdSetScissor(cmdList->Buffer, 0, 1, &scissor);
}

void RenderDevice::ClearColorRect(CommandListHandle cmd, Color color,
                                   i32 x, i32 y, u32 width, u32 height)
{
    // vkCmdClearAttachments writes directly into the active render pass without
    // ending it — valid inside vkCmdBeginRendering (Vulkan 1.3 dynamic rendering).
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList, "ClearColorRect: invalid command list");

    VkClearAttachment attach{};
    attach.aspectMask                  = VK_IMAGE_ASPECT_COLOR_BIT;
    attach.colorAttachment             = 0; // index into the color attachment array
    attach.clearValue.color.float32[0] = color.R;
    attach.clearValue.color.float32[1] = color.G;
    attach.clearValue.color.float32[2] = color.B;
    attach.clearValue.color.float32[3] = color.A;

    VkClearRect rect{};
    rect.rect           = { { x, y }, { width, height } };
    rect.baseArrayLayer = 0;
    rect.layerCount     = 1;

    vkCmdClearAttachments(cmdList->Buffer, 1, &attach, 1, &rect);
}

void RenderDevice::Draw(CommandListHandle cmd, u32 vertexCount, u32 firstVertex,
                         u32 instanceCount, u32 firstInstance)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList, "Draw: invalid command list");
    vkCmdDraw(cmdList->Buffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderDevice::DrawIndexed(CommandListHandle cmd, u32 indexCount, u32 firstIndex,
                                 i32 vertexOffset, u32 instanceCount, u32 firstInstance)
{
    VulkanCommandList* cmdList = _context->CommandLists.Get(cmd);
    ARCBIT_ASSERT(cmdList, "DrawIndexed: invalid command list");
    vkCmdDrawIndexed(cmdList->Buffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

// ---------------------------------------------------------------------------
// Submit & Present
// ---------------------------------------------------------------------------

// Submit command lists to the GPU with swapchain synchronisation.
//
// Waits on ImageAvailable[CurrentFrame] before executing (ensures the image
// is not still being read by the display hardware), then signals RenderFinished
// when done. InFlight is signalled so AcquireNextImage can detect when this
// frame slot is free for reuse.
void RenderDevice::Submit(std::initializer_list<CommandListHandle> commands)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(_context->ActiveSwapchain);
    ARCBIT_ASSERT(sc != nullptr, "Submit: no active swapchain");

    std::vector<VkCommandBuffer> cmdBuffers;
    for (auto handle : commands)
    {
        VulkanCommandList* cmdList = _context->CommandLists.Get(handle);
        if (cmdList) cmdBuffers.push_back(cmdList->Buffer);
        _context->CommandLists.Free(handle); // handle is consumed by Submit
    }

    // The wait stage tells Vulkan at which pipeline stage to block until the
    // semaphore is signalled.  COLOR_ATTACHMENT_OUTPUT is the earliest stage
    // that writes to a color attachment — we don't need to block sooner.
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &sc->ImageAvailable[sc->CurrentFrame];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = static_cast<u32>(cmdBuffers.size());
    submit.pCommandBuffers      = cmdBuffers.data();
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &sc->RenderFinished[sc->CurrentImageIndex]; // per-image, not per-frame-slot

    // Signal InFlight so AcquireNextImage knows when this frame slot is free.
    vkQueueSubmit(_context->GraphicsQueue, 1, &submit, sc->InFlight[sc->CurrentFrame]);
}

// Present the current swapchain image to the display.
// Waits on RenderFinished (set by Submit) before presenting.
// Advances CurrentFrame so the next AcquireNextImage uses a fresh sync slot.
void RenderDevice::Present(SwapchainHandle handle)
{
    VulkanSwapchain* sc = _context->Swapchains.Get(handle);
    ARCBIT_ASSERT(sc != nullptr, "Present: invalid handle");

    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &sc->RenderFinished[sc->CurrentImageIndex]; // per-image, matches Submit
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &sc->Swapchain;
    presentInfo.pImageIndices      = &sc->CurrentImageIndex;

    const VkResult result = vkQueuePresentKHR(_context->PresentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        LOG_WARN(Render, "Present: swapchain suboptimal - resize on next frame");

    // Advance the frame slot index so the next frame uses different sync objects.
    sc->CurrentFrame = (sc->CurrentFrame + 1) % MaxFramesInFlight;
}

// Block until all pending GPU work is complete.
// Use only during shutdown or resource rebuilding — not per-frame.
void RenderDevice::WaitIdle()
{
    if (_context && _context->Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(_context->Device);
}

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Factory functions — must be outside namespace Arcbit (C linkage + namespaces
// are incompatible). Arcbit_ prefix avoids symbol collisions.
// ---------------------------------------------------------------------------
extern "C"
{

ARCBIT_RENDER_API Arcbit::RenderDevice* Arcbit_CreateDevice(const Arcbit::DeviceDesc& desc)
{
    auto context = std::make_unique<Arcbit::VulkanContext>();
    if (!context->Init(desc))
    {
        LOG_ERROR(Render, "Arcbit_CreateDevice: backend initialisation failed");
        return nullptr;
    }
    return new Arcbit::RenderDevice(std::move(context));
}

ARCBIT_RENDER_API void Arcbit_DestroyDevice(Arcbit::RenderDevice* device)
{
    delete device;
}

} // extern "C"
