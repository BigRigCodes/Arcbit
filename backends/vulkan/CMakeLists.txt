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

# vulkan-1.dll (the Vulkan loader) comes with a GPU driver. Load it at the
# first Vulkan call rather than at startup, so programs that link the backend
# but never make one - the headless tests, on a CI machine with no GPU - start
# without it. The game makes the first call creating its device.
if(MSVC)
    target_link_options(arcbit-vulkan PRIVATE "/DELAYLOAD:vulkan-1.dll")
    target_link_libraries(arcbit-vulkan PRIVATE delayimp)
endif()
