#include <arcbit/app/UserFolder.h>

#include <arcbit/core/Log.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <string>
#include <system_error>

namespace Arcbit
{
std::filesystem::path UserDataFolder(const std::string_view studio, const std::string_view game)
{
    char* path = SDL_GetPrefPath(std::string(studio).c_str(), std::string(game).c_str());
    if (!path) {
        LOG_WARN(Engine, "No per-user data folder from the OS - using ./userdata");
        std::error_code ec;
        std::filesystem::create_directories("userdata", ec);
        return "userdata";
    }
    std::filesystem::path folder = std::filesystem::u8path(path);
    SDL_free(path);
    return folder;
}

void MoveLegacyFile(const std::filesystem::path& legacy, const std::filesystem::path& current)
{
    std::error_code ec;
    if (std::filesystem::exists(current, ec) || !std::filesystem::exists(legacy, ec)) return;
    std::filesystem::create_directories(current.parent_path(), ec);
    if (std::filesystem::copy_file(legacy, current, ec))
        LOG_INFO(Engine, "Moved '{}' to '{}'", legacy.string(), current.string());
}
} // namespace Arcbit
