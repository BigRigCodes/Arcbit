#pragma once

#include <arcbit/core/Types.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// What one loot entry gives.
enum class LootKind : u8 { Item, Currency, Table, Nothing };

// One way a roll can go: an item or a currency (Min..Max of it), another
// table (rolled Min..Max times), or nothing - chosen by Weight against the
// table's other entries.
struct LootEntry
{
    LootKind    Kind = LootKind::Nothing;
    std::string Id;
    u32         Min    = 1;
    u32         Max    = 1;
    u32         Weight = 1;
};

struct LootTable
{
    std::string            Id;
    u32                    RollsMin = 1; // weighted picks from Entries, Min..Max of them
    u32                    RollsMax = 1;
    std::vector<LootEntry> Always;  // given on every roll of the table, before the picks
    std::vector<LootEntry> Entries; // the weighted ones
};

// What rolling gave: an item or a currency, and how many.
struct LootStack
{
    std::string Id;
    u32         Count    = 0;
    bool        Currency = false;

    bool operator==(const LootStack&) const = default;
};

// A mistake in a loot file: errors are entries it can't use (an unknown
// item, a loop of tables), warnings likely slips (a misspelled field).
struct LootIssue
{
    std::string Table; // empty for the file as a whole
    std::string File;
    bool        Error = true;
    std::string Message;
};

// ---------------------------------------------------------------------------
// LootLibrary - every loot table in every .arcloot file in a folder and
// below, by id (docs/items.md#loot-tables). Many tables to a file:
//
//   { "slime": { "rolls": [1, 2],
//                "always":  [ { "currency": "gold", "count": [1, 5] } ],
//                "entries": [ { "item": "potion", "weight": 3 },
//                             { "item": "old_boot", "count": [1, 2] },
//                             { "table": "gems", "weight": 1 },
//                             { "weight": 6 } ] } }
//
// An entry names an "item", a "currency" or a "table", or none of them -
// nothing. "count" is a number or [min, max] (default 1); "weight" (default
// 1) is its share of the picks. "rolls" says how many picks, likewise.
// ---------------------------------------------------------------------------
class LootLibrary
{
public:
    static constexpr std::string_view Extension = ".arcloot";

    // Log problems as they're found (GetIssues has them either way).
    bool LogIssues = true;

    // Returns how many tables the folder's files hold (a missing folder: 0).
    u32 AddFolder(const std::filesystem::path& folder);

    // A table from code (tests, a game's generated ones); replaces one of that id.
    void Add(std::string id, const nlohmann::json& table);

    [[nodiscard]] const LootTable*         Find(std::string_view id) const; // null if there's none
    [[nodiscard]] std::vector<std::string> GetIds() const;                 // sorted

    // Every entry's item, currency and table checked - unknown ones are
    // errors, as is a table that rolls itself, however deep. Run once the
    // items and currencies are known.
    void Check(const std::function<bool(std::string_view)>& isItem,
               const std::function<bool(std::string_view)>& isCurrency);

    [[nodiscard]] const std::vector<LootIssue>& GetIssues() const { return _issues; }

    // Roll a table: its "always" entries, then its picks; the same item (or
    // currency) from several entries comes back as one stack, in the order
    // first given. An unknown table gives nothing (logged).
    [[nodiscard]] std::vector<LootStack> Roll(std::string_view id, std::mt19937& rng) const;

private:
    [[nodiscard]] LootTable Parse(const std::string& id, const nlohmann::json& table, const std::string& file);
    [[nodiscard]] LootEntry ParseEntry(const std::string& table, const nlohmann::json& entry, const std::string& file);
    void ReadRange(const nlohmann::json& value, u32& min, u32& max, const std::string& table, const std::string& file);
    void RollInto(const LootTable& table, std::mt19937& rng, std::vector<LootStack>& out, u32 depth) const;
    void Give(const LootEntry& entry, std::mt19937& rng, std::vector<LootStack>& out, u32 depth) const;
    [[nodiscard]] bool Loops(const std::string& id, std::vector<std::string>& path) const;
    void Issue(std::string table, std::string file, bool error, std::string message);

    std::map<std::string, LootTable, std::less<>>   _tables;
    std::map<std::string, std::string, std::less<>> _files; // table -> the file it came from
    std::vector<LootIssue>                           _issues;
};
} // namespace Arcbit
