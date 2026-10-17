// Save thumbnails: a captured frame scaled down and written as a PNG next to
// its slot. Split out of SaveManager.cpp so the stb image code stays here.

#include <arcbit/save/SaveManager.h>

#include <arcbit/core/Log.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <fstream>

namespace Arcbit
{
namespace
{
    // Appends what stb writes to the vector in `context`.
    void AppendBytes(void* context, void* data, const int size)
    {
        auto*       out   = static_cast<std::vector<u8>*>(context);
        const auto* bytes = static_cast<const u8*>(data);
        out->insert(out->end(), bytes, bytes + size);
    }
} // namespace

std::filesystem::path SaveManager::ThumbnailOf(const SaveSlot slot) const
{
    return std::filesystem::path(PathOf(slot)).replace_extension(".png");
}

// Scaled to ThumbnailWidth (keeping the aspect; never up), encoded to memory
// - stb's own file writer can't open non-ASCII paths on Windows - then
// written beside the save. Not sealed: a picture gives nothing away.
bool SaveManager::WriteThumbnail(const SaveSlot slot, const u32 width, const u32 height, const std::span<const u8> rgba)
{
    if (width == 0 || height == 0 || rgba.size() < u64(width) * height * 4) return false;
    const u32       outW = std::min(width, ThumbnailWidth);
    const u32       outH = std::max(1u, static_cast<u32>(u64(height) * outW / width));
    std::vector<u8> small(u64(outW) * outH * 4);
    stbir_resize_uint8_srgb(rgba.data(), static_cast<int>(width), static_cast<int>(height), 0, small.data(),
                            static_cast<int>(outW), static_cast<int>(outH), 0, STBIR_RGBA);

    std::vector<u8> png;
    if (!stbi_write_png_to_func(AppendBytes, &png, static_cast<int>(outW), static_cast<int>(outH), 4, small.data(),
                                static_cast<int>(outW * 4))) {
        LOG_WARN(Engine, "SaveManager: couldn't encode the thumbnail for '{}'", ToString(slot));
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(_directory, ec);
    std::ofstream file(ThumbnailOf(slot), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file.flush()) {
        LOG_WARN(Engine, "SaveManager: can't write '{}'", ThumbnailOf(slot).string());
        return false;
    }
    return true;
}
} // namespace Arcbit
