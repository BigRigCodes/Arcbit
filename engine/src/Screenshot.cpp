#include "Screenshot.h"

#include <arcbit/core/Log.h>

#include <stb_image_write.h> // the implementation is in SaveThumbnail.cpp

#include <chrono>
#include <format>
#include <fstream>
#include <vector>

namespace Arcbit
{
namespace
{
void AppendBytes(void* context, void* data, const int size)
{
    auto*       out   = static_cast<std::vector<u8>*>(context);
    const auto* bytes = static_cast<const u8*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::filesystem::path FreeName(const std::filesystem::path& folder)
{
    const auto now  = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto stem = std::format("screenshot-{:%Y%m%d-%H%M%S}",
                                  std::chrono::zoned_time{ std::chrono::current_zone(), now });
    std::filesystem::path path = folder / (stem + ".png");
    for (int n = 2; std::filesystem::exists(path); ++n) path = folder / std::format("{}-{}.png", stem, n);
    return path;
}
} // namespace

// Encoded to memory and written with a stream: stb's own file writer can't
// open non-ASCII paths on Windows (a user folder can have any name).
std::optional<std::filesystem::path> WriteScreenshot(const std::filesystem::path& folder, const CapturedFrame& frame)
{
    std::vector<u8> png;
    if (frame.Width == 0 || frame.Height == 0 ||
        !stbi_write_png_to_func(AppendBytes, &png, static_cast<int>(frame.Width), static_cast<int>(frame.Height), 4,
                                frame.Rgba.data(), static_cast<int>(frame.Width * 4))) {
        LOG_WARN(Engine, "Screenshot: couldn't encode the frame");
        return std::nullopt;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto    path = FreeName(folder);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file.flush()) {
        LOG_WARN(Engine, "Screenshot: can't write '{}'", path.string());
        return std::nullopt;
    }
    LOG_INFO(Engine, "Screenshot: saved '{}'", path.string());
    return path;
}
} // namespace Arcbit
