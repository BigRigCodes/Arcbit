#include "VulkanContext.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Anonymous helpers
// ---------------------------------------------------------------------------
namespace {

// Returns true if every layer in `requested` is present in the Vulkan instance
// layer list. Logs each missing layer individually so the user knows exactly
// what is absent on their system.
bool CheckLayerSupport(const std::vector<const char*>& requested)
{
    // Two-call idiom: first call returns the count, second fills the array.
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (const char* name : requested)
    {
        bool found = false;
        for (const auto& layer : available)
        {
            if (strcmp(name, layer.layerName) == 0) { found = true; break; }
        }
        if (!found)
        {
            LOG_ERROR(Render, "Validation layer not available: {}", name);
            return false;
        }
    }
    return true;
}

// Score a physical device for suitability. Higher scores are preferred.
// Returns -1 if the device fails any hard requirement — the caller will skip it.
//
// Hard requirements (score -1 if missing):
//   • Vulkan 1.3 API — we rely on dynamic rendering + synchronization2 (both 1.3 core).
//   • VK_KHR_swapchain — needed to present frames to the display.
//   • dynamicRendering feature — avoids VkRenderPass objects; lets us describe
//     attachments inline in the command buffer with vkCmdBeginRendering.
//   • synchronization2 feature — cleaner pipeline barrier API (VkPipelineStageFlags2).
//   • At least one queue family that supports both graphics and present.
//
// Soft scoring:
//   • +1000 for discrete GPU (dedicated VRAM, faster than integrated).
//   • +100  for integrated GPU (still hardware-accelerated).
i32 ScoreDevice(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    // Hard check: must be Vulkan 1.3 or newer.
    if (props.apiVersion < VK_API_VERSION_1_3) return -1;

    // Hard check: must expose VK_KHR_swapchain for window presentation.
    u32 extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

    bool hasSwapchain = false;
    for (const auto& ext : exts)
    {
        if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            hasSwapchain = true;
            break;
        }
    }
    if (!hasSwapchain) return -1;

    // Hard check: must support dynamic rendering and synchronization2.
    // vkGetPhysicalDeviceFeatures2 with a pNext chain lets us query 1.3 features
    // without needing separate extension structs.
    VkPhysicalDeviceVulkan13Features feats13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 feats2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feats13 };
    vkGetPhysicalDeviceFeatures2(device, &feats2);

    if (!feats13.dynamicRendering || !feats13.synchronization2) return -1;

    // Hard check: need at least one queue family that handles both graphics work
    // and presentation to our surface. On most desktop hardware, family 0 does both.
    u32 queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

    bool hasGraphicsAndPresent = false;
    for (u32 i = 0; i < queueCount; ++i)
    {
        if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) { hasGraphicsAndPresent = true; break; }
    }
    if (!hasGraphicsAndPresent) return -1;

    // Soft score: prefer a dedicated discrete GPU over an integrated one.
    i32 score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;

    return score;
}

// Validation layer message callback — routes Vulkan messages into the engine Log.
// Vulkan calls this for every validation warning, error, and general diagnostic.
//
// We return VK_FALSE unconditionally — returning VK_TRUE would instruct Vulkan
// to abort the offending call, which is only useful when debugging the driver itself.
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(Render, "(validation) {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(Render, "(validation) {}", data->pMessage);
    else
        LOG_DEBUG(Render, "(validation) {}", data->pMessage);

    return VK_FALSE;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// VulkanContext::Init
//
// Brings up the full Vulkan backend in dependency order.
// Any single failure returns false; the caller (Arcbit_CreateDevice) treats
// that as a fatal error and returns nullptr to the application.
// ---------------------------------------------------------------------------
bool VulkanContext::Init(const DeviceDesc& desc)
{
    LOG_INFO(Render, "Initialising Vulkan backend");

    // 1. Create the VkInstance — the root Vulkan object that loads the runtime
    //    and extension layers. Extensions needed by SDL for surface creation are
    //    queried from SDL and added automatically in CreateInstance.
    if (!CreateInstance(desc.AppName, desc.AppVersion, desc.EnableValidation))
        return false;

    // 2. Optionally attach the debug messenger so validation messages flow into Log.
    if (desc.EnableValidation)
        SetupDebugMessenger();

    // 3. Create a temporary WSI surface from the application window.
    //    We need this early so SelectPhysicalDevice can verify that the chosen
    //    GPU can actually present to our window — this is not guaranteed on all
    //    platforms or multi-monitor setups. We store the surface and hand it off
    //    to the swapchain later via CreateSwapchain.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(
            static_cast<SDL_Window*>(desc.NativeWindowHandle),
            Instance, nullptr, &surface))
    {
        LOG_ERROR(Render, "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    // 4. Enumerate all physical devices, score them, and pick the best one.
    //    ScoreDevice rejects anything that doesn't meet minimum requirements.
    if (!SelectPhysicalDevice(surface))
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // 5. Create the logical device (VkDevice) with the feature set we need.
    //    Retrieves queue handles at the same time.
    if (!CreateLogicalDevice())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // 6. Create the VMA allocator. VMA wraps vkAllocateMemory and suballocates
    //    from large blocks to stay within driver heap limits.
    if (!CreateAllocator())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // 7. Create the command pool from which we allocate command buffers.
    //    Using RESET_COMMAND_BUFFER_BIT so we can reset per-buffer, not per-pool.
    if (!CreateCommandPool())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // 8. Create the global descriptor pool and texture set layout used for
    //    sampled texture binding (one VkDescriptorSet per texture).
    if (!CreateDescriptorPool() || !CreateDescriptorSetLayout())
    {
        vkDestroySurfaceKHR(Instance, surface, nullptr);
        return false;
    }

    // Hold the surface until CreateSwapchain is called — it will take ownership.
    m_InitSurface = surface;

    LOG_INFO(Render, "Vulkan backend ready -- {}",
        PhysicalDeviceProps.deviceName);
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::Shutdown
//
// Destroys all Vulkan objects in reverse-init order to satisfy Vulkan's
// requirement that child objects are destroyed before their parents.
// Must be called after vkDeviceWaitIdle so no GPU work is still in flight.
// ---------------------------------------------------------------------------
void VulkanContext::Shutdown()
{
    // Block until the GPU has finished all submitted work.
    // Required before destroying any resource the GPU may still be reading.
    vkDeviceWaitIdle(Device);

    // Descriptor infrastructure — must go before the logical device.
    if (StorageBufferSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, StorageBufferSetLayout, nullptr);
    if (TextureSetLayout       != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, TextureSetLayout,       nullptr);
    if (GlobalDescriptorPool   != VK_NULL_HANDLE) vkDestroyDescriptorPool     (Device, GlobalDescriptorPool,   nullptr);

    // Command pool must be destroyed before the logical device.
    if (CommandPool  != VK_NULL_HANDLE) vkDestroyCommandPool(Device, CommandPool, nullptr);

    // VMA must be destroyed before the logical device (it holds references to it).
    if (Allocator    != VK_NULL_HANDLE) vmaDestroyAllocator(Allocator);

    // Logical device — all child objects (queues are implicitly destroyed here).
    if (Device       != VK_NULL_HANDLE) vkDestroyDevice(Device, nullptr);

    // Surface — if no swapchain was ever created, we still own m_InitSurface.
    // If a swapchain was created, it should have taken ownership and nulled this.
    if (m_InitSurface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(Instance, m_InitSurface, nullptr);

    // Debug messenger — must be destroyed before the instance.
    // The function pointer is not part of the statically loaded Vulkan dispatch
    // table, so we must look it up via vkGetInstanceProcAddr at destroy time too.
    if (DebugMessenger != VK_NULL_HANDLE)
    {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(Instance, DebugMessenger, nullptr);
    }

    // Instance — the last Vulkan object to go.
    if (Instance != VK_NULL_HANDLE) vkDestroyInstance(Instance, nullptr);

    LOG_INFO(Render, "Vulkan backend shut down");
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateInstance
//
// Creates the VkInstance, which loads the Vulkan runtime and connects us to
// any enabled layers. SDL provides the platform-specific surface extensions
// (e.g. VK_KHR_win32_surface on Windows) — we must enable them here or SDL
// won't be able to create a surface for our window.
// ---------------------------------------------------------------------------
bool VulkanContext::CreateInstance(const char* appName, u32 appVersion, bool enableValidation)
{
    // Query SDL for the extensions it needs to create a Vulkan surface.
    // On Windows this is typically ["VK_KHR_surface", "VK_KHR_win32_surface"].
    u32 sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
    std::vector<const char*> layers;

    if (enableValidation)
    {
        // VK_EXT_debug_utils provides the messenger API and object naming.
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        // VK_LAYER_KHRONOS_validation intercepts all Vulkan calls and validates
        // arguments, synchronisation, resource lifetimes, etc.
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        if (!CheckLayerSupport(validationLayers))
        {
            // Non-fatal — continue without validation rather than refusing to start.
            LOG_WARN(Render, "Validation layers requested but not available - continuing without");
        }
        else
        {
            layers = validationLayers;
        }
    }

    // VkApplicationInfo is purely informational — the driver may use it to
    // apply game-specific optimisations or workarounds, but it doesn't affect
    // correctness. Setting apiVersion to 1.3 signals that we understand 1.3 rules.
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName   = appName;
    appInfo.applicationVersion = appVersion;
    appInfo.pEngineName        = "Arcbit";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames     = layers.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateInstance failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VkInstance created");
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::SetupDebugMessenger
//
// Registers our DebugCallback with the instance so validation messages are
// routed to the engine Log. The function pointer is not in the standard Vulkan
// dispatch table — it lives in the validation layer — so we must look it up at
// runtime via vkGetInstanceProcAddr. Failure is non-fatal (some stripped driver
// builds don't include the extension even when we asked for it).
// ---------------------------------------------------------------------------
void VulkanContext::SetupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    // Subscribe to warnings and errors. Verbose/info messages are very noisy
    // and typically only needed when debugging the driver or layers themselves.
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    // Receive all three message categories: general API usage, validation rule
    // violations, and performance warnings (e.g. redundant pipeline barriers).
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;

    // Look up the extension function at runtime.
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));

    if (fn) fn(Instance, &info, nullptr, &DebugMessenger);
}

// ---------------------------------------------------------------------------
// VulkanContext::SelectPhysicalDevice
//
// Enumerates all Vulkan-capable GPUs on the system, scores each one using
// ScoreDevice, and selects the best. Also records the graphics and present
// queue family indices that we will use when creating the logical device.
// ---------------------------------------------------------------------------
bool VulkanContext::SelectPhysicalDevice(VkSurfaceKHR surface)
{
    // Two-call pattern: first get the count, then fill the array.
    u32 count = 0;
    vkEnumeratePhysicalDevices(Instance, &count, nullptr);
    if (count == 0)
    {
        LOG_ERROR(Render, "No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(Instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    i32 bestScore = -1;

    for (auto& device : devices)
    {
        const i32 score = ScoreDevice(device, surface);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        LOG_DEBUG(Render, "  GPU candidate: {} (score {})", props.deviceName, score);

        if (score > bestScore) { bestScore = score; best = device; }
    }

    if (best == VK_NULL_HANDLE)
    {
        LOG_ERROR(Render, "No suitable GPU found (requires Vulkan 1.3 + dynamic rendering + sync2)");
        return false;
    }

    PhysicalDevice = best;
    vkGetPhysicalDeviceProperties(PhysicalDevice, &PhysicalDeviceProps);
    vkGetPhysicalDeviceFeatures(PhysicalDevice, &PhysicalDeviceFeats);

    // Record queue family indices — we need these to create queues in CreateLogicalDevice
    // and to know which family owns the command pool.
    u32 queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueCount, queues.data());

    for (u32 i = 0; i < queueCount; ++i)
    {
        // Record the first graphics-capable family we find.
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && GraphicsFamily == ~0u)
            GraphicsFamily = i;

        // Record the first family that can present to our surface.
        // On most desktop hardware this is the same family as graphics.
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, i, surface, &presentSupport);
        if (presentSupport && PresentFamily == ~0u)
            PresentFamily = i;

        // Stop as soon as both families are found.
        if (GraphicsFamily != ~0u && PresentFamily != ~0u) break;
    }

    LOG_INFO(Render, "Selected GPU: {} (graphics family {}, present family {})",
        PhysicalDeviceProps.deviceName, GraphicsFamily, PresentFamily);

    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateLogicalDevice
//
// Creates the VkDevice — our primary interface to the physical hardware.
// We enable exactly the extensions and features we need:
//   • VK_KHR_swapchain — for window presentation.
//   • dynamicRendering — inline attachment descriptions via vkCmdBeginRendering,
//     eliminating the need for VkRenderPass and VkFramebuffer objects.
//   • synchronization2 — cleaner pipeline barriers with 64-bit stage/access flags.
//
// Features are passed via a pNext chain. VkPhysicalDeviceVulkan13Features
// covers both dynamicRendering and synchronization2 since they are 1.3 core.
// ---------------------------------------------------------------------------
bool VulkanContext::CreateLogicalDevice()
{
    // Deduplicate queue families — if graphics and present are the same family,
    // we only create one VkDeviceQueueCreateInfo (Vulkan requires uniqueness).
    std::vector<u32> uniqueFamilies = { GraphicsFamily };
    if (PresentFamily != GraphicsFamily)
        uniqueFamilies.push_back(PresentFamily);

    // Priority 1.0 gives this queue the highest scheduling priority within its family.
    const f32 queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (u32 family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &queuePriority;
        queueInfos.push_back(qi);
    }

    // Enable the Vulkan 1.3 features we rely on.
    // This struct is chained into VkDeviceCreateInfo via pNext so the driver
    // knows to activate these features when creating the logical device.
    VkPhysicalDeviceVulkan13Features feats13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    feats13.dynamicRendering = VK_TRUE; // vkCmdBeginRendering instead of VkRenderPass
    feats13.synchronization2 = VK_TRUE; // VkPipelineStageFlags2 / VkAccessFlags2

    // Wrap in Features2 so we can attach the 1.3 struct via pNext.
    VkPhysicalDeviceFeatures2 feats2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feats13 };

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.pNext                   = &feats2; // feature chain
    createInfo.queueCreateInfoCount    = static_cast<u32>(queueInfos.size());
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;
    // Note: enabledLayerCount/ppEnabledLayerNames are deprecated in Vulkan 1.1+;
    // layers are now instance-level only.

    const VkResult result = vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateDevice failed ({})", static_cast<i32>(result));
        return false;
    }

    // Retrieve handles to the queues we created. Index 0 = the first (and only)
    // queue in each family that we requested.
    vkGetDeviceQueue(Device, GraphicsFamily, 0, &GraphicsQueue);
    vkGetDeviceQueue(Device, PresentFamily,  0, &PresentQueue);

    LOG_DEBUG(Render, "VkDevice created");
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateAllocator
//
// Initialises Vulkan Memory Allocator (VMA). VMA handles all GPU memory
// management: it suballocates from large VkDeviceMemory blocks, selects the
// right heap for each usage pattern (VRAM vs RAM-backed), and optionally
// defragments. Without it we'd hit the driver's vkAllocateMemory limit (~4096
// allocations on some implementations) very quickly.
// ---------------------------------------------------------------------------
bool VulkanContext::CreateAllocator()
{
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3; // tell VMA which API level to target
    info.instance         = Instance;
    info.physicalDevice   = PhysicalDevice;
    info.device           = Device;

    const VkResult result = vmaCreateAllocator(&info, &Allocator);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vmaCreateAllocator failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VMA allocator created");
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateCommandPool
//
// Creates the command pool from which we allocate VkCommandBuffers.
//
// RESET_COMMAND_BUFFER_BIT: lets us reset and re-record individual command
// buffers each frame without resetting the entire pool. Without this flag,
// resetting any one buffer would implicitly reset all others in the pool.
//
// This is a single pool tied to the graphics queue family. In future we will
// expand to one pool per thread to enable multi-threaded command recording.
// ---------------------------------------------------------------------------
bool VulkanContext::CreateCommandPool()
{
    VkCommandPoolCreateInfo info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    info.queueFamilyIndex = GraphicsFamily;
    // RESET_COMMAND_BUFFER_BIT: allows per-command-buffer reset rather than per-pool.
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    const VkResult result = vkCreateCommandPool(Device, &info, nullptr, &CommandPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateCommandPool failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "VkCommandPool created");
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateDescriptorPool
//
// Creates a shared descriptor pool sized for two descriptor types:
//   - COMBINED_IMAGE_SAMPLER: one set per sampled texture (up to 1024).
//   - STORAGE_BUFFER: one set per frame-in-flight per storage buffer (up to
//     256 × MaxFramesInFlight slots, covers many SSBOs with headroom).
//
// FREE_DESCRIPTOR_SET_BIT lets us free individual sets when resources are
// destroyed, rather than having to reset the entire pool.
// ---------------------------------------------------------------------------
bool VulkanContext::CreateDescriptorPool()
{
    const VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          256 },
    };

    VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 1024 + 256;
    info.poolSizeCount = static_cast<u32>(std::size(poolSizes));
    info.pPoolSizes    = poolSizes;

    const VkResult result = vkCreateDescriptorPool(Device, &info, nullptr, &GlobalDescriptorPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Render, "vkCreateDescriptorPool failed ({})", static_cast<i32>(result));
        return false;
    }

    LOG_DEBUG(Render, "Descriptor pool created (1024 sampler + 256 SSBO slots)");
    return true;
}

// ---------------------------------------------------------------------------
// VulkanContext::CreateDescriptorSetLayout
//
// Creates two descriptor set layouts shared by all pipelines:
//
//   TextureSetLayout        — binding 0, COMBINED_IMAGE_SAMPLER, fragment stage.
//                             Reused for both albedo (set 0) and normal map (set 1).
//
//   StorageBufferSetLayout  — binding 0, STORAGE_BUFFER, vertex + fragment stages.
//                             Used for the per-frame dynamic light list (set 2).
// ---------------------------------------------------------------------------
bool VulkanContext::CreateDescriptorSetLayout()
{
    // --- Texture layout (combined image sampler, fragment only) ---
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 1;
        info.pBindings    = &binding;

        const VkResult result = vkCreateDescriptorSetLayout(Device, &info, nullptr, &TextureSetLayout);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Render, "vkCreateDescriptorSetLayout (texture) failed ({})", static_cast<i32>(result));
            return false;
        }
    }

    // --- Storage buffer layout (SSBO, vertex + fragment) ---
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        // Both stages need access: vertex reads light positions, fragment computes contribution.
        binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 1;
        info.pBindings    = &binding;

        const VkResult result = vkCreateDescriptorSetLayout(Device, &info, nullptr, &StorageBufferSetLayout);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Render, "vkCreateDescriptorSetLayout (storage buffer) failed ({})", static_cast<i32>(result));
            return false;
        }
    }

    LOG_DEBUG(Render, "Descriptor set layouts created (texture + storage buffer)");
    return true;
}

} // namespace Arcbit
