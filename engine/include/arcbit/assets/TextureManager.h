#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Arcbit {

class RenderDevice;

// ---------------------------------------------------------------------------
// TextureInfo
//
// Metadata returned by TextureManager::GetInfo().
// ---------------------------------------------------------------------------
struct TextureInfo
{
    u32 Width  = 0;
    u32 Height = 0;
};

// RGBA8 pixels decoded from an image file, not yet on the GPU.
struct DecodedImage
{
    u32             Width  = 0;
    u32             Height = 0;
    std::vector<u8> Pixels; // Width * Height * 4 bytes
};

// ---------------------------------------------------------------------------
// TextureManager
//
// Loads 2D textures from disk (PNG, JPG, BMP, TGA via stb_image) and
// caches them by canonical file path so duplicate loads are free.
//
// Game code should NOT call Load() directly. Load textures through
// SpriteSheet::Load() instead — this ensures every texture has associated
// metadata (frames, animations, pivot points, pixels_per_unit) and keeps the
// asset pipeline consistent. In Phase 36, SpriteSheet will dispatch to
// .arcasset binary files in release builds while continuing to use JSON in dev.
//
// TextureManager::Load() remains public so SpriteSheet and other engine
// systems can drive it internally. Unload() and CheckReloads() are valid to
// call from game code for explicit cache management and hot-reload control.
//
// Typical usage
// -------------
//   // OnStart — load via SpriteSheet, not directly:
//   _floorSheet = SpriteSheet::Load("assets/textures/floor.json", GetTextures());
//
//   // OnShutdown — not required; Application calls Clear() automatically.
//   // To release one texture explicitly:
//   GetTextures().Unload(handle);
//
// Hot-reload
// ----------
//   CheckReloads() polls every loaded texture's file modification time.
//   If a file has changed, its pixels are re-uploaded to the same
//   TextureHandle — all existing users see the update automatically.
//   Application calls this once per second from the game loop.
//   Only works when texture dimensions are unchanged (same size on disk).
//
// Thread safety
// -------------
//   NOT thread-safe. Call from the main/game thread only. The exceptions are
//   the static Decode() and NormalizePath(), which a loader thread uses to
//   decode images ahead of time; the main thread hands the results to Stage()
//   so the next Load / Acquire of that path only uploads.
// ---------------------------------------------------------------------------
class TextureManager
{
public:
    explicit TextureManager(RenderDevice& device);
    ~TextureManager();

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Load a texture from disk and cache it, or return the cached handle if
    // already loaded. The path is normalized so equivalent paths hit the cache.
    // Returns TextureHandle::Invalid() if the file cannot be opened or decoded.
    [[nodiscard]] TextureHandle Load(std::string_view path);

    // Force a reload of a previously loaded texture from its original path.
    // The handle remains valid — only the GPU contents are updated.
    // No-op if the handle is not in the cache or dimensions have changed.
    void Reload(TextureHandle handle);

    // Release a texture and remove it from the cache.
    // The handle becomes invalid after this call.
    void Unload(TextureHandle handle);

    // Counted load for scene-lifetime textures (Scene::LoadTexture). Each
    // Acquire needs one Release; the texture is destroyed when the count
    // reaches zero, unless Load() also pinned it for the app's lifetime.
    // Release destroys GPU resources, so call it only while the render thread
    // is idle (RenderThread::WaitIdle) — SceneManager does.
    [[nodiscard]] TextureHandle Acquire(std::string_view path);
    void                        Release(TextureHandle handle);

    // Release all cached textures and clear the cache.
    // Called automatically by Application during shutdown.
    void Clear();

    // Poll all loaded textures for file changes and re-upload modified ones.
    // Application calls this once per second. Can also be called manually.
    // `beforeReload` runs once, before the first re-upload, only when a file
    // actually changed — Application passes RenderThread::WaitIdle so the
    // upload doesn't race a frame in flight.
    void CheckReloads(const std::function<void()>& beforeReload = {});

    // The normalized path a cached texture was loaded from; empty if unknown.
    [[nodiscard]] std::string GetPath(TextureHandle handle) const;

    // Returns width/height of a cached texture, or zeroes if not found.
    [[nodiscard]] TextureInfo GetInfo(TextureHandle handle) const;

    // --- Loading ahead (scene streaming) ------------------------------------

    // The cache key for a path: separators and redundant components
    // normalized, so equivalent spellings share one entry. Thread-safe.
    [[nodiscard]] static std::string NormalizePath(std::string_view path);

    // Read and decode an image file to RGBA8 without touching the GPU.
    // Thread-safe. Null (and logged) if the file can't be read or decoded.
    [[nodiscard]] static std::optional<DecodedImage> Decode(const std::string& path);

    // Normalized paths of every cached texture - lets a loader thread skip
    // decoding what is already on the GPU.
    [[nodiscard]] std::unordered_set<std::string> GetLoadedPaths() const;

    // Hand over images decoded ahead of time, keyed by normalized path. A
    // later Load / Acquire of one of those paths uploads it instead of reading
    // the file. Paths already cached are ignored.
    void Stage(std::unordered_map<std::string, DecodedImage> images);

    // Drop staged images nothing loaded. Returns how many were dropped.
    size_t ClearStaged();

private:
    struct CacheEntry
    {
        TextureHandle                     Handle;
        std::string                       Path;
        u32                               Width        = 0;
        u32                               Height       = 0;
        std::filesystem::file_time_type   LastModified = {};
        u32                               RefCount     = 0;     // live Acquire()s
        bool                              Pinned       = false; // loaded with Load(): never auto-freed
    };

    // Cache lookup by normalized path, loading from disk on a miss. Null when
    // the file fails to load.
    [[nodiscard]] CacheEntry* FindOrLoad(std::string_view path);

    // Decode the file at the given (already-normalized) path - or take its
    // staged image - upload to GPU, insert into both caches, and return the
    // new handle.
    [[nodiscard]] TextureHandle LoadFromDisk(const std::string& path);

    // Upload decoded pixels as a new cached texture.
    [[nodiscard]] TextureHandle Upload(const std::string& path, const DecodedImage& image);

    // Re-decode and re-upload a cached entry (same handle, new pixels).
    void ReloadFromDisk(const std::string& path);

    RenderDevice& _device;

    // Primary lookup: normalized path → cache entry.
    std::unordered_map<std::string, CacheEntry> _pathCache;

    // Reverse lookup: handle.Value → path (for Unload and Reload).
    std::unordered_map<u32, std::string> _handleToPath;

    // Images decoded ahead of time (Stage), waiting for their first load.
    std::unordered_map<std::string, DecodedImage> _staged;
};

} // namespace Arcbit
