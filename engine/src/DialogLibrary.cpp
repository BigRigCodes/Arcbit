#include <arcbit/dialog/DialogLibrary.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <format>
#include <ranges>

namespace Arcbit
{
namespace
{
std::filesystem::file_time_type StampOf(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto      stamp = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : stamp;
}

// Loc's name for a dialog's strings.
std::string SourceOf(const std::string_view id) { return std::format("dialog:{}", id); }
} // namespace

u32 DialogLibrary::AddFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return 0;
    u32 count = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!item.is_regular_file() || item.path().extension() != Extension) continue;
        std::string id = std::filesystem::relative(item.path(), folder, ec).replace_extension().generic_string();
        Entry&      entry = _dialogs[id];
        entry.File        = item.path();
        Read(id, entry);
        ++count;
    }
    LOG_INFO(Engine, "Dialog: {} dialog(s) in '{}'", count, folder.string());
    return count;
}

// A file that no longer reads keeps its last good dialog.
void DialogLibrary::Read(const std::string& id, Entry& entry)
{
    entry.Stamp = StampOf(entry.File);
    entry.Issues.clear();
    if (auto dialog = LoadDialog(entry.File, id, entry.Issues)) {
        entry.Loaded = std::make_shared<const Dialog>(std::move(*dialog));
        const auto strings = entry.Loaded->GetStrings();
        Loc::SetStrings(Loc::English, SourceOf(id), strings);
    }
    for (const DialogIssue& issue : entry.Issues) {
        if (!LogIssues) break;
        if (issue.Error) LOG_ERROR(Engine, "Dialog '{}': {}", id, issue.Message);
        else LOG_WARN(Engine, "Dialog '{}': {}", id, issue.Message);
    }
}

std::shared_ptr<const Dialog> DialogLibrary::Get(const std::string_view id) const
{
    const auto it = _dialogs.find(id);
    if (it == _dialogs.end() || !it->second.Loaded) {
        LOG_WARN(Engine, "Dialog: there's no dialog '{}'", id);
        return nullptr;
    }
    return it->second.Loaded;
}

std::vector<std::string> DialogLibrary::GetIds() const
{
    std::vector<std::string> ids;
    for (const auto& [id, entry] : _dialogs)
        if (entry.Loaded) ids.push_back(id);
    return ids;
}

std::vector<std::string> DialogLibrary::GetAllIds() const
{
    std::vector<std::string> ids;
    for (const std::string& id : _dialogs | std::views::keys) ids.push_back(id);
    return ids;
}

const std::vector<DialogIssue>& DialogLibrary::GetIssues(const std::string_view id) const
{
    static const std::vector<DialogIssue> none;
    const auto                            it = _dialogs.find(id);
    return it != _dialogs.end() ? it->second.Issues : none;
}

std::filesystem::path DialogLibrary::GetFile(const std::string_view id) const
{
    const auto it = _dialogs.find(id);
    return it != _dialogs.end() ? it->second.File : std::filesystem::path{};
}

u32 DialogLibrary::CheckReloads()
{
    u32 reloaded = 0;
    for (auto& [id, entry] : _dialogs) {
        if (StampOf(entry.File) == entry.Stamp) continue;
        Read(id, entry);
        LOG_INFO(Engine, "Dialog: reloaded '{}'", id);
        ++reloaded;
    }
    return reloaded;
}

void DialogLibrary::Clear()
{
    for (const std::string& id : _dialogs | std::views::keys) Loc::SetStrings(Loc::English, SourceOf(id), {});
    _dialogs.clear();
}
} // namespace Arcbit
