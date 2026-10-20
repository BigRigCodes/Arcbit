#pragma once

#include <arcbit/dialog/Dialog.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// DialogLibrary - every .arcdialog in a folder and below, by id: its path
// in the folder without the extension ("elder", "town/guard").
//
// Each file is read and checked when its folder is added (they're small),
// its problems logged, and its English given to Loc (Loc::SetStrings), so
// every dialog line has a string before anything asks for one - and the
// locale tools see them. A folder added later wins on the same id.
// ---------------------------------------------------------------------------
class DialogLibrary
{
public:
    static constexpr std::string_view Extension = ".arcdialog";

    // Log each file's problems as it's read (tools that report them their
    // own way - arcbit-loc - turn it off; GetIssues has them either way).
    bool LogIssues = true;

    // Returns how many dialogs were read (a missing folder is fine: 0).
    u32 AddFolder(const std::filesystem::path& folder);

    // The dialog, or null (logged). A runner keeps the one it started with
    // through a reload.
    [[nodiscard]] std::shared_ptr<const Dialog> Get(std::string_view id) const;
    [[nodiscard]] bool                          Has(std::string_view id) const { return _dialogs.contains(id); }
    [[nodiscard]] std::vector<std::string>      GetIds() const;    // the ones that loaded, sorted
    [[nodiscard]] std::vector<std::string>      GetAllIds() const; // every file read, loaded or not, sorted

    // What's wrong with a dialog's file, from its last read.
    [[nodiscard]] const std::vector<DialogIssue>& GetIssues(std::string_view id) const;
    [[nodiscard]] std::filesystem::path           GetFile(std::string_view id) const; // empty if unknown

    // Read again the files changed on disk since they were read; returns how
    // many (debug builds poll it, so edits show without a restart).
    u32 CheckReloads();

    // Forget every dialog and take their strings out of Loc.
    void Clear();

private:
    struct Entry
    {
        std::filesystem::path           File;
        std::filesystem::file_time_type Stamp;
        std::shared_ptr<const Dialog>   Loaded; // the last good read
        std::vector<DialogIssue>        Issues;
    };

    void Read(const std::string& id, Entry& entry);

    std::map<std::string, Entry, std::less<>> _dialogs;
};
} // namespace Arcbit
