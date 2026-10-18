# arcbit-loc: checks a game's localization against its content and writes
# translator templates (docs/localization.md).
add_executable(arcbit-loc main.cpp)
target_link_libraries(arcbit-loc PRIVATE arcbit-engine arcbit-vulkan)
