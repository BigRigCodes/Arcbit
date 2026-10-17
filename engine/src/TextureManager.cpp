#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <arcbit/assets/TextureManager.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/core/Log.h>

#include <filesystem>

namespace Arcbit {

TextureManager::TextureManager(RenderDevice& device)
    : _device(device)
{}

TextureManager::~TextureManager()
{
    // Clear() requires a live device — Application calls it explicitly in the
    // shutdown sequence before DestroyDevice, so this should already be empty.
    Clear();
}

// ---------------------------------------------------------------------------
// Load / Unload
// ---------------------------------------------------------------------------

TextureManager::CacheEntry* TextureManager::FindOrLoad(const std::string_view path)
{
    // Normalize separators and remove redundant components so that
    // "assets/textures/../textures/foo.png" and "assets/textures/foo.png"
    // both map to the same cache entry.
    const std::string key = std::filesystem::path(path).lexically_normal().string();

    // Cache hit — return the existing entry without re-uploading.
    if (const auto it = _pathCache.find(key); it != _pathCache.end()) return &it->second;

    if (!LoadFromDisk(key).IsValid()) return nullptr;
    return &_pathCache.at(key);
}

TextureHandle TextureManager::Load(const std::string_view path)
{
    CacheEntry* entry = FindOrLoad(path);
    if (!entry) return TextureHandle::Invalid();
    entry->Pinned = true;
    return entry->Handle;
}

TextureHandle TextureManager::Acquire(const std::string_view path)
{
    CacheEntry* entry = FindOrLoad(path);
    if (!entry) return TextureHandle::Invalid();
    ++entry->RefCount;
    return entry->Handle;
}

void TextureManager::Release(const TextureHandle handle)
{
    const auto handleIt = _handleToPath.find(handle.Value);
    if (handleIt == _handleToPath.end()) return;

    CacheEntry& entry = _pathCache.at(handleIt->second);
    if (entry.RefCount > 0) --entry.RefCount;
    if (entry.RefCount == 0 && !entry.Pinned) Unload(handle);
}

void TextureManager::Reload(TextureHandle handle)
{
    const auto it = _handleToPath.find(handle.Value);
    if (it == _handleToPath.end())
        return;

    ReloadFromDisk(it->second);
}

void TextureManager::Unload(TextureHandle handle)
{
    const auto handleIt = _handleToPath.find(handle.Value);
    if (handleIt == _handleToPath.end())
        return;

    const std::string path = handleIt->second;
    _handleToPath.erase(handleIt);

    if (const auto pathIt = _pathCache.find(path); pathIt != _pathCache.end()) {
        _device.DestroyTexture(pathIt->second.Handle);
        _pathCache.erase(pathIt);
    }
}

void TextureManager::Clear()
{
    for (const auto& [path, entry] : _pathCache)
        _device.DestroyTexture(entry.Handle);

    _pathCache.clear();
    _handleToPath.clear();
}

// ---------------------------------------------------------------------------
// Hot-reload
// ---------------------------------------------------------------------------

void TextureManager::CheckReloads(const std::function<void()>& beforeReload)
{
    bool quiesced = false;
    for (auto& [path, entry] : _pathCache) {
        std::error_code ec;
        const auto currentTime = std::filesystem::last_write_time(path, ec);
        if (ec) continue; // file deleted or inaccessible — skip silently
        if (currentTime == entry.LastModified) continue;

        if (!quiesced && beforeReload) beforeReload();
        quiesced = true;
        ReloadFromDisk(path);
    }
}

// ---------------------------------------------------------------------------
// Info query
// ---------------------------------------------------------------------------

std::string TextureManager::GetPath(const TextureHandle handle) const
{
    const auto it = _handleToPath.find(handle.Value);
    return it != _handleToPath.end() ? it->second : std::string{};
}

TextureInfo TextureManager::GetInfo(TextureHandle handle) const
{
    const auto it = _handleToPath.find(handle.Value);
    if (it == _handleToPath.end())
        return {};

    const auto entryIt = _pathCache.find(it->second);
    if (entryIt == _pathCache.end())
        return {};

    return TextureInfo{ entryIt->second.Width, entryIt->second.Height };
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

TextureHandle TextureManager::LoadFromDisk(const std::string& path)
{
    int w, h, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_ERROR(Engine, "TextureManager: failed to load '{}' - {}", path, stbi_failure_reason());
        return TextureHandle::Invalid();
    }

    TextureDesc desc{};
    desc.Width     = static_cast<u32>(w);
    desc.Height    = static_cast<u32>(h);
    desc.Format    = Format::RGBA8_UNorm;
    desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    desc.DebugName = path.c_str();

    const TextureHandle handle = _device.CreateTexture(desc);
    if (!handle.IsValid()) {
        stbi_image_free(pixels);
        LOG_ERROR(Engine, "TextureManager: GPU texture creation failed for '{}'", path);
        return TextureHandle::Invalid();
    }

    _device.UploadTexture(handle, pixels, static_cast<u64>(w) * h * 4);
    stbi_image_free(pixels);

    CacheEntry entry{};
    entry.Handle       = handle;
    entry.Path         = path;
    entry.Width        = static_cast<u32>(w);
    entry.Height       = static_cast<u32>(h);
    entry.LastModified = std::filesystem::last_write_time(path);

    _pathCache.emplace(path, entry);
    _handleToPath.emplace(handle.Value, path);

    LOG_DEBUG(Engine, "TextureManager: loaded '{}' ({}x{})", path, w, h);
    return handle;
}

void TextureManager::ReloadFromDisk(const std::string& path)
{
    auto it = _pathCache.find(path);
    if (it == _pathCache.end()) return;

    CacheEntry& entry = it->second;

    int w, h, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_WARN(Engine, "TextureManager: hot-reload failed for '{}' - {}", path, stbi_failure_reason());
        return;
    }

    if (static_cast<u32>(w) == entry.Width && static_cast<u32>(h) == entry.Height) {
        // Same dimensions — re-upload to the existing handle so all users
        // automatically see the new content without handle invalidation.
        _device.UploadTexture(entry.Handle, pixels, static_cast<u64>(w) * h * 4);
        entry.LastModified = std::filesystem::last_write_time(path);
        LOG_INFO(Engine, "TextureManager: hot-reloaded '{}'", path);
    } else {
        // Dimension change would invalidate the handle and break existing users.
        // Log a warning and leave the old texture in place.
        LOG_WARN(Engine, "TextureManager: skipping hot-reload for '{}' - "
                 "dimensions changed ({}x{} -> {}x{})",
                 path, entry.Width, entry.Height, w, h);
    }

    stbi_image_free(pixels);
}

} // namespace Arcbit
