#pragma once

#include <arcbit/items/Item.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// ItemLibrary - every item in every .arcitems file in a folder and below, by
// id (ids are global: the same id in two places is an error).
//
// An item can build on another - "extends": "potion" - in any file: its own
// fields go over the parent's, nested ones ("stats") field by field. An
// "abstract": true item is only for extending; it isn't an item itself.
//
// Each item's English name and description go to Loc (Loc::SetStrings) as
// they're read, so they have strings before anything asks - and the locale
// tools see them.
// ---------------------------------------------------------------------------
class ItemLibrary
{
public:
    static constexpr std::string_view Extension = ".arcitems";

    // What an item's fields are checked and completed against - the
    // project's (ProjectDefaults); set before adding folders.
    struct Rules
    {
        std::map<std::string, u32, std::less<>> StackByType; // an item without "stack"
        u32                                     Stack = 99;  // ... of a type not listed
        std::vector<std::string>                SlotKinds;   // what an equipment's "slot" may name; empty: anything

        // Where icon paths are found, to check they exist; nullopt: not checked.
        std::optional<std::filesystem::path> AssetRoot;
    };

    Rules ItemRules;

    // Log each item's problems as they're found (tools that report them
    // their own way - arcbit-loc - turn it off; GetIssues has them either way).
    bool LogIssues = true;

    // Returns how many items the folder's files hold (a missing folder: 0).
    u32 AddFolder(const std::filesystem::path& folder);

    [[nodiscard]] const ItemDef*           Find(std::string_view id) const; // null if there's none
    [[nodiscard]] bool                     Has(std::string_view id) const { return Find(id) != nullptr; }
    [[nodiscard]] std::vector<std::string> GetIds() const; // sorted
    [[nodiscard]] usize                    Count() const { return _items.size(); }

    // Everything wrong with the files, from their last read.
    [[nodiscard]] const std::vector<ItemIssue>& GetIssues() const { return _issues; }
    [[nodiscard]] std::filesystem::path         GetFile(std::string_view id) const; // empty if unknown

    // Read again the files changed on disk since; returns how many (debug
    // builds poll it, so edits show without a restart).
    u32 CheckReloads();

    // Forget every item and take their strings out of Loc.
    void Clear();

private:
    struct File
    {
        std::filesystem::path           Path;
        std::filesystem::file_time_type Stamp;
        nlohmann::ordered_json          Items; // as written
        std::string                     ParseError; // empty when it read
    };
    struct Source
    {
        const nlohmann::ordered_json* Json = nullptr;
        const File*                   From = nullptr;
    };

    void ReadFile(File& file);
    void Build();
    [[nodiscard]] std::map<std::string, Source> Gather();
    [[nodiscard]] std::optional<nlohmann::json> Resolve(const std::string& id, const std::map<std::string, Source>& sources);
    [[nodiscard]] ItemDef Parse(const std::string& id, const nlohmann::json& item, const File& file);
    void ReadGrid(ItemDef& def, const nlohmann::json& item, const File& file);
    void ReadEquipment(ItemDef& def, const nlohmann::json& item, const File& file);
    void CheckIcon(const ItemDef& def, const File& file);
    void PublishStrings() const;
    void Issue(std::string item, const File* file, bool error, std::string message);

    std::vector<File>                              _files;
    std::map<std::string, ItemDef, std::less<>>    _items;
    std::map<std::string, std::filesystem::path, std::less<>> _itemFiles;
    std::vector<ItemIssue>                         _issues;
};
} // namespace Arcbit
