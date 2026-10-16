# Overlay of vcpkg's stock x64-windows triplet, pinned to the project's
# compiler toolset.
#
# Without the pin, vcpkg builds ports with the newest Visual Studio installed
# (VS 18 / v145 on this machine) while the project compiles and links with
# VS 2022 (v143). MSVC only supports linking libraries built by an older or
# equal toolset; v145-built ports reference newer STL internals the v143
# runtime libraries lack (e.g. Catch2: unresolved __std_find_last_not_ch_pos_1).
#
# Moving the project to a newer Visual Studio means bumping this value too.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PLATFORM_TOOLSET v143)
