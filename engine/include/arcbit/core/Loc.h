#pragma once

#include <arcbit/core/Types.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Loc - the player-visible strings, in the player's language.
//
// Every language is a set of locale files named by its code (`en.json`,
// `fr.json`, `pt-BR.json`) in the folders added with AddFolder - the engine's
// first, then the game's, whose strings win. A file is a JSON object of
// key -> string; nested objects join their keys with dots, so
// { "ui": { "pause": { "title": "PAUSED" } } } is "ui.pause.title".
// An optional "$locale" block describes the language:
//   "$locale": { "name": "Francais", "fonts": ["../fonts/Extra.otf"] }
// `name` is shown in the language picker (in that language); `fonts` are
// fallback faces the language needs, relative to the file's folder.
//
// Get looks a key up in the active language, then the language it's a
// region of (`pt-BR` -> `pt`), then English, and returns the key itself if
// none has it - so a half-translated game shows English, never a blank.
// Main thread only.
// ---------------------------------------------------------------------------
class Loc
{
public:
    // The base language, always loaded: every key should exist in it.
    static constexpr std::string_view English = "en";

    struct Locale
    {
        std::string              Code;  // as its file is named: "fr", "pt-BR"
        std::string              Name;  // "$locale.name", else the code
        std::vector<std::string> Fonts; // "$locale.fonts", resolved paths
    };

    // Find the locale files in folder (a missing folder is fine). Languages
    // already loaded are read again, so a game's folder added late still wins.
    static void AddFolder(const std::filesystem::path& folder);

    // Every language found, English first, the rest by code.
    [[nodiscard]] static std::vector<Locale> GetLocales();
    [[nodiscard]] static const Locale*      FindLocale(std::string_view code); // case and '_' / '-' don't matter

    // Make code the active language. False (nothing changes) if no file has it.
    static bool                             SetLanguage(std::string_view code);
    [[nodiscard]] static const std::string& GetLanguage();

    // Changes whenever what Get returns may have: a language switch, a file
    // loaded. Text that was looked up can compare it to know it's stale.
    [[nodiscard]] static u64 GetGeneration();

    // Merge one file's strings into the active language (on top of its files).
    // Returns false and logs a warning if it can't be read or parsed.
    [[nodiscard]] static bool Load(std::string_view path);

    // The string for key; the key itself if no language in the chain has it.
    // The reference stays valid until the strings change (GetGeneration).
    [[nodiscard]] static const std::string& Get(std::string_view key);
    [[nodiscard]] static bool               Has(std::string_view key); // in the active chain

    // Forget every folder, language and string (tests; a full reload).
    static void Clear();
};

} // namespace Arcbit
