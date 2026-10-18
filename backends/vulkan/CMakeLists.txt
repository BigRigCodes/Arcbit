add_library(arcbit-vulkan SHARED
    src/VmaImpl.cpp
    src/VulkanContext.cpp
    src/VulkanDevice.cpp
)

target_compile_definitions(arcbit-vulkan PRIVATE
    ARCBIT_VULKAN_EXPORTS
)

# Vulkan Memory Allocator from vcpkg (header-only), not the Vulkan SDK's
# optional copy - every machine and CI build the same version.
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

target_include_directories(arcbit-vulkan PRIVATE
    src/
    ${Vulkan_INCLUDE_DIRS}   # Include/vulkan/
)

target_link_libraries(arcbit-vulkan PRIVATE
    arcbit-engine            # render API headers + core headers
    arcbit-core              # logging, asserts
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    SDL3::SDL3
)
