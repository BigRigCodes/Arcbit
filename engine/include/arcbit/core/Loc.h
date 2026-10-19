#pragma once

#include <arcbit/core/Types.h>

#include <concepts>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Arcbit
{
// A named value for a placeholder in a localized string - text or a number:
//   Loc::Format("ui.saves.auto", {{ "index", 3 }})
struct LocArg
{
    LocArg(const std::string_view name, const std::string_view text) : Name(name), Text(text) {}
    LocArg(const std::string_view name, const char* text) : Name(name), Text(text) {}
    LocArg(const std::string_view name, const std::string& text) : Name(name), Text(text) {}

    template <typename T>
        requires std::is_arithmetic_v<T> && (!std::same_as<T, bool>)
    LocArg(const std::string_view name, const T number)
        : Name(name), Text(std::format("{}", number)), Number(static_cast<f64>(number)) {}

    std::string        Name;
    std::string        Text;   // what replaces {Name}
    std::optional<f64> Number; // set for numbers: a "count" picks the plural form
};

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
//
// Format fills placeholders by name - "Autosave {index}" - so a translator
// can move them; `{{` and `}}` are literal braces, and a placeholder with no
// argument stays as written, so it's noticed. Numbers take the language's
// separators ("1,234.5", "1 234,5"); a spec after a colon changes that:
// {value:n2} (two decimals), {saved:date} / {saved:time} / {saved:datetime}
// (a Unix time), {year:raw} (exactly as passed). With a number named "count",
// a key with plural forms picks one by the language's rules:
//   "item.coins": { "one": "{count} coin", "other": "{count} coins" }
// The forms are CLDR's: zero, one, two, few, many, other (every language has
// other). Main thread only.
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

    // The first of `preferred` (codes, most wanted first: the OS's languages)
    // the game has: exactly ("fr-CA"), else by language ("fr-CA" -> "fr").
    [[nodiscard]] static std::optional<std::string> Match(std::span<const std::string> preferred);

    // Make code the active language. False (nothing changes) if no file has it.
    static bool                             SetLanguage(std::string_view code);
    [[nodiscard]] static const std::string& GetLanguage();

    // Changes whenever what Get returns may have: a language switch, a file
    // loaded. Text that was looked up can compare it to know it's stale.
    [[nodiscard]] static u64 GetGeneration();

    // Merge one file's strings into the active language (on top of its files).
    // Returns false and logs a warning if it can't be read or parsed.
    [[nodiscard]] static bool Load(std::string_view path);

    // Strings for a language from something other than its locale files -
    // the English in dialog files. source names the set: setting it again
    // replaces it, an empty list removes it. A language's files win over these.
    static void SetStrings(std::string_view code, std::string_view source,
                           std::span<const std::pair<std::string, std::string>> strings);

    // The string for key; the key itself if no language in the chain has it.
    // The reference stays valid until the strings change (GetGeneration).
    [[nodiscard]] static const std::string& Get(std::string_view key);
    [[nodiscard]] static bool               Has(std::string_view key); // in the active chain

    // The string for key with its placeholders filled (and its plural form
    // picked by a "count" argument).
    [[nodiscard]] static std::string Format(std::string_view key, std::initializer_list<LocArg> args);
    [[nodiscard]] static std::string Format(std::string_view key, std::span<const LocArg> args);

    // Fill placeholders in text that isn't a key.
    [[nodiscard]] static std::string Interpolate(std::string_view text, std::span<const LocArg> args);

    // A number in the language's style: its decimal and group separators
    // ("loc.decimal", "loc.group", grouping from "loc.group_from" digits).
    // decimals < 0: as many as the number has.
    [[nodiscard]] static std::string FormatNumber(f64 value, i32 decimals = -1);

    // A Unix time as local "date", "time" or "datetime", by the language's
    // patterns ("loc.date": "{month_short} {day}, {year}") and month names
    // ("loc.month.1", "loc.month_short.1").
    [[nodiscard]] static std::string FormatDate(i64 unixSeconds, std::string_view style = "date");

    // The CLDR plural form ("one", "few", "other", ...) of n in a language.
    // Languages without built-in rules count like English.
    [[nodiscard]] static std::string_view PluralForm(std::string_view language, f64 n);

    // A language's own strings (its files, not what it falls back to), as
    // key / text pairs sorted by key; empty for a language the game lacks.
    [[nodiscard]] static std::vector<std::pair<std::string, std::string>> GetStrings(std::string_view code);

    // English keys a language (default: the active one) has no string for,
    // itself or through the language it's a region of - what's left to
    // translate. Sorted.
    [[nodiscard]] static std::vector<std::string> GetMissing(std::string_view code = {});

    // Read again the files of every loaded language that changed on disk
    // since they were read. Returns how many languages reloaded (debug
    // builds call it once a second, so edits show without a restart).
    static u32 CheckReloads();

    // Every string the active language can show (its own, then the ones it
    // falls back to) - to bake their glyphs ahead (FontAtlas::Prepare).
    static void ForEachString(const std::function<void(std::string_view)>& visit);

    // The pseudo-locale "qps": every English string accented, padded about
    // 35% and bracketed ("[Šáví Ğáñé ~~~~]"), so text that isn't localized
    // and layouts too tight for a longer translation stand out. Debug builds
    // add it; it follows English as English reloads.
    static constexpr std::string_view PseudoCode = "qps";
    static void                       AddPseudoLocale();
    [[nodiscard]] static std::string  Pseudo(std::string_view english);

    // Forget every folder, language and string (tests; a full reload).
    static void Clear();

private:
    // One placeholder's text: {name} or {name:spec} (LocFormat.cpp).
    [[nodiscard]] static std::string FormatArg(const LocArg& arg, std::string_view spec);
};

} // namespace Arcbit
