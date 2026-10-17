#pragma once

#include <filesystem>
#include <string_view>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// The game's per-user data folder - settings and saves - in the place the OS
// expects game data: %APPDATA%\<studio>\<game>\ on Windows,
// ~/.local/share/<studio>/<game>/ on Linux, ~/Library/Application
// Support/<studio>/<game>/ on macOS (SDL_GetPrefPath). Created if missing.
// It survives reinstalls, needs no write access to the install folder, and
// is per user. Falls back to ./userdata if the OS gives no folder (logged).
// ---------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path UserDataFolder(std::string_view studio, std::string_view game);

// Settings used to live next to the executable. If `current` doesn't exist
// yet but `legacy` does, copy it over once, so players keep their settings.
void MoveLegacyFile(const std::filesystem::path& legacy, const std::filesystem::path& current);
} // namespace Arcbit
