#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/items/Item.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Checks a game's localization against its content (the arcbit-loc tool,
// and the editor later): keys the content uses that English lacks, English
// keys nothing uses, raw text in layouts, translations English no longer
// has, each language's coverage, and the dialog files' problems.
//
// Dialog files (docs/dialog.md) and item files (docs/items.md) carry their
// own English: it's loaded into Loc like a locale file's, so dialog lines and
// item names count in coverage and templates and are used by their file.
//
// Content is scanned as text: .arcui / .arcscene / .arcprefab ("..._key" fields and
// "..._keys" arrays, raw "text" / "label" / "placeholder"), .lua (Loc.Get /
// Format / Has, TextKey =), .cpp / .h (Loc::Get / Format, SetTextKey /
// SetLabelKey), and other .json (credits) for keys it mentions. A key counts
// as used when it, or the key it extends ("demo.world" for
// "demo.world.name"), is mentioned anywhere; keys built from smaller pieces
// can't be seen, so Unused is a list to look over, not to delete blindly.
namespace Arcbit::LocCheck
{
struct Place
{
    std::string Text; // the key, or the raw text
    std::string File;
    u32         Line = 0;
};

struct Coverage
{
    std::string Code;
    std::string Name;
    usize       Translated = 0; // of English's keys, with a string of its own (or its base language's)
    usize       Total      = 0;
};

// A translation of a key English doesn't have - often a dialog line that was
// renumbered or removed; the translation is lost until it's moved.
struct StaleKey
{
    std::string Code; // the language
    std::string Key;
};

// Something wrong with a dialog file: its own checks (docs/dialog.md), or
// English for one of its lines that a locale file overrides.
struct DialogProblem
{
    std::string Dialog; // its id
    std::string File;
    bool        Error = true;
    std::string Message;
};

struct Report
{
    std::vector<Place>         Missing; // used, but English has no such key (nor plural forms of it)
    std::vector<std::string>   Unused;  // English keys no scanned file mentions
    std::vector<Place>         RawText; // player text written into a layout instead of a key
    std::vector<StaleKey>      Stale;
    std::vector<DialogProblem> Dialogs;
    std::vector<ItemIssue>     Items;
    std::vector<Coverage>      Languages;

    [[nodiscard]] bool HasDialogErrors() const;
    [[nodiscard]] bool HasItemErrors() const;
};

// Load the locale, dialog and item folders (into Loc, replacing what it
// held) and check the content under the scan roots against them.
[[nodiscard]] Report Check(std::span<const std::filesystem::path> localeFolders,
                           std::span<const std::filesystem::path> scanRoots,
                           std::span<const std::filesystem::path> dialogFolders = {},
                           std::span<const std::filesystem::path> itemFolders   = {});

// A starting file for translating into a new language: "$locale" with an
// empty name, then every English key with its English text to replace.
// Loc must hold the locales (Check, or Loc::AddFolder).
[[nodiscard]] std::string Template();
} // namespace Arcbit::LocCheck
