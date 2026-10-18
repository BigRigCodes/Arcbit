#pragma once

#include <arcbit/render/RenderThread.h>

#include <filesystem>
#include <optional>

namespace Arcbit
{
// Write a captured frame into folder as screenshot-YYYYMMDD-HHMMSS.png (a
// number added if that's taken). Returns the file written, or nothing.
std::optional<std::filesystem::path> WriteScreenshot(const std::filesystem::path& folder, const CapturedFrame& frame);
} // namespace Arcbit
