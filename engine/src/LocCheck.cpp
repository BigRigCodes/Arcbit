#include <arcbit/core/LocCheck.h>
#include <arcbit/core/Loc.h>
#include <arcbit/dialog/DialogLibrary.h>
#include <arcbit/items/ItemLibrary.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace Arcbit::LocCheck
{
namespace
{
// Layouts, scenes and prefabs: JSON whose "..._key" fields name keys.
bool IsContentJson(const std::string& extension)
{
    return extension == ".arcui" || extension == ".arcscene" || extension == ".arcprefab";
}

// The patterns that use a key, by file type; the key is the first group.
const std::vector<std::regex>& UsePatterns(const std::string& extension)
{
    static const std::vector<std::regex> json = { std::regex(R"re("\w+_key"\s*:\s*"([^"]+)")re") };
    static const std::vector<std::regex> lua  = { std::regex(R"re(Loc\.(?:Get|Format|Has)\(\s*["']([^"']+)["'])re"),
                                                  std::regex(R"re(TextKey\s*=\s*["']([^"']+)["'])re") };
    static const std::vector<std::regex> cpp  = { std::regex(R"re(Loc::(?:Get|Format|Has)\(\s*"([^"]+)")re"),
                                                  std::regex(R"re(Set(?:Text|Label)Key\(\s*"([^"]+)")re") };
    static const std::vector<std::regex> none;
    if (IsContentJson(extension)) return json;
    if (extension == ".lua") return lua;
    if (extension == ".cpp" || extension == ".h") return cpp;
    return none;
}

// Anything quoted that's shaped like a key ("ui.pause.title"): what Unused
// looks for, so a key passed through a helper still counts as used.
const std::regex& DottedLiteral()
{
    static const std::regex literal(R"re(["']([A-Za-z0-9_]+(?:\.[A-Za-z0-9_\-]+)+)["'])re");
    return literal;
}

// A layout's "text" / "label" / "placeholder" with words in it: player text
// that skipped the locale files ("<", "50%" and "..." don't count).
const std::regex& RawTextPattern()
{
    static const std::regex raw(R"re("(?:text|label|placeholder)"\s*:\s*"([^"]*[A-Za-z]{2}[^"]*)")re");
    return raw;
}

// "item_keys": [ "a", "b" ] - an array of keys, on one line.
const std::regex& KeyArray()
{
    static const std::regex array(R"re("\w+_keys"\s*:\s*\[([^\]]*)\])re");
    return array;
}

struct Scan
{
    std::vector<Place>    Uses;
    std::set<std::string> Literals;
    std::vector<Place>    RawText;

    void Use(std::string key, const std::string& file, const u32 line)
    {
        Literals.insert(key); // a used key needn't be dotted ("coins")
        Uses.push_back({ std::move(key), file, line });
    }
};

void ScanLine(const std::string& line, const std::string& file, const u32 number, const std::string& extension, Scan& scan)
{
    static const std::regex quoted(R"re("([^"]+)")re");
    for (const std::regex& pattern : UsePatterns(extension))
        for (std::sregex_iterator it(line.begin(), line.end(), pattern), end; it != end; ++it) scan.Use((*it)[1].str(), file, number);
    for (std::sregex_iterator it(line.begin(), line.end(), DottedLiteral()), end; it != end; ++it) scan.Literals.insert((*it)[1].str());
    if (!IsContentJson(extension)) return;
    for (std::sregex_iterator it(line.begin(), line.end(), KeyArray()), end; it != end; ++it) {
        const std::string keys = (*it)[1].str();
        for (std::sregex_iterator key(keys.begin(), keys.end(), quoted), last; key != last; ++key) scan.Use((*key)[1].str(), file, number);
    }
    if (extension == ".arcui")
        for (std::sregex_iterator it(line.begin(), line.end(), RawTextPattern()), end; it != end; ++it)
            scan.RawText.push_back({ (*it)[1].str(), file, number });
}

// Other JSON content (credits, data files) can mention keys too; the locale
// files themselves don't count.
void ScanFile(const std::filesystem::path& file, Scan& scan)
{
    const std::string extension = file.extension().string();
    if (UsePatterns(extension).empty() && extension != ".json") return;
    std::ifstream in(file);
    std::string   line;
    for (u32 number = 1; std::getline(in, line); ++number) ScanLine(line, file.generic_string(), number, extension, scan);
}

Scan ScanRoots(const std::span<const std::filesystem::path> roots, const std::span<const std::filesystem::path> skip)
{
    std::vector<std::filesystem::path> skipped;
    for (const auto& folder : skip) skipped.push_back(std::filesystem::weakly_canonical(folder));
    const auto inSkipped = [&](const std::filesystem::path& file) {
        const auto folder = std::filesystem::weakly_canonical(file).parent_path();
        return std::ranges::find(skipped, folder) != skipped.end();
    };

    Scan            scan;
    std::error_code ec;
    for (const auto& root : roots) {
        if (std::filesystem::is_regular_file(root, ec)) {
            ScanFile(root, scan);
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != decltype(it){}; it.increment(ec))
            if (it->is_regular_file() && !inSkipped(it->path())) ScanFile(it->path(), scan);
    }
    return scan;
}

// "item.coins.one" -> "item.coins": a plural form is used through its key,
// and "demo.world.name" may be built from "demo.world" at run time.
std::string Parent(const std::string& key)
{
    const auto dot = key.rfind('.');
    return dot != std::string::npos ? key.substr(0, dot) : key;
}

std::vector<std::string> EnglishKeys()
{
    std::vector<std::string> keys;
    for (auto& [key, text] : Loc::GetStrings(Loc::English)) keys.push_back(std::move(key));
    return keys;
}

// One dialog's keys count as used (by it), and a speaker without a name in
// the file needs its key from the locale files; English the locale files
// give one of its lines replaces the dialog's - worth knowing.
void ScanDialog(const Dialog& dialog, const std::string& file, const std::map<std::string, std::string>& fileEnglish,
                Scan& scan, Report& report)
{
    for (const auto& [key, text] : dialog.GetStrings()) {
        scan.Literals.insert(key);
        if (const auto it = fileEnglish.find(key); it != fileEnglish.end() && it->second != text)
            report.Dialogs.push_back({ dialog.Id, file, false,
                                       std::format("'{}': the English locale files replace this line's text", key) });
    }
    for (const DialogSpeaker& speaker : dialog.Speakers)
        if (speaker.English.empty()) scan.Use(speaker.Key, file, 0);
}

// The dialog files: their English into Loc (under the locale files'), their
// problems into the report.
void CheckDialogs(const std::span<const std::filesystem::path> folders, Scan& scan, Report& report)
{
    std::map<std::string, std::string> fileEnglish;
    for (auto& [key, text] : Loc::GetStrings(Loc::English)) fileEnglish.emplace(std::move(key), std::move(text));
    DialogLibrary library;
    library.LogIssues = false;
    for (const auto& folder : folders) (void)library.AddFolder(folder);
    for (const std::string& id : library.GetAllIds()) {
        const std::string file = library.GetFile(id).generic_string();
        for (const DialogIssue& issue : library.GetIssues(id)) report.Dialogs.push_back({ id, file, issue.Error, issue.Message });
        if (library.Has(id)) ScanDialog(*library.Get(id), file, fileEnglish, scan, report);
    }
}

// The item files: their English into Loc, their keys used (by them), their
// problems into the report - and English a locale file replaces, noted.
void CheckItems(const std::span<const std::filesystem::path> folders, Scan& scan, Report& report)
{
    std::map<std::string, std::string> fileEnglish;
    for (auto& [key, text] : Loc::GetStrings(Loc::English)) fileEnglish.emplace(std::move(key), std::move(text));
    ItemLibrary library;
    library.LogIssues = false;
    for (const auto& folder : folders) (void)library.AddFolder(folder);
    report.Items = library.GetIssues();
    for (const std::string& id : library.GetIds()) {
        const ItemDef& item = *library.Find(id);
        for (const auto& [key, text] : { std::pair{ item.NameKey, item.Name }, std::pair{ item.DescriptionKey, item.Description } }) {
            scan.Literals.insert(key);
            if (const auto it = fileEnglish.find(key); it != fileEnglish.end() && it->second != text && !text.empty())
                report.Items.push_back({ id, library.GetFile(id).generic_string(), false,
                                         std::format("'{}': the English locale files replace this text", key) });
        }
    }
}

// Translations English doesn't have (a language's own plural forms aside).
std::vector<StaleKey> FindStale(const std::set<std::string>& english)
{
    std::vector<StaleKey> stale;
    for (const Loc::Locale& locale : Loc::GetLocales()) {
        if (locale.Code == Loc::English) continue;
        for (const auto& [key, text] : Loc::GetStrings(locale.Code))
            if (!english.contains(key) && !english.contains(Parent(key) + ".other")) stale.push_back({ locale.Code, key });
    }
    return stale;
}
} // namespace

bool Report::HasDialogErrors() const
{
    return std::ranges::any_of(Dialogs, &DialogProblem::Error);
}

bool Report::HasItemErrors() const { return std::ranges::any_of(Items, &ItemIssue::Error); }

Report Check(const std::span<const std::filesystem::path> localeFolders,
             const std::span<const std::filesystem::path> scanRoots,
             const std::span<const std::filesystem::path> dialogFolders,
             const std::span<const std::filesystem::path> itemFolders)
{
    Loc::Clear();
    for (const auto& folder : localeFolders) Loc::AddFolder(folder);
    Report report;
    Scan   scan = ScanRoots(scanRoots, localeFolders);
    CheckDialogs(dialogFolders, scan, report);
    CheckItems(itemFolders, scan, report);
    const std::vector<std::string> english = EnglishKeys();
    const std::set<std::string>    known(english.begin(), english.end());

    for (const Place& use : scan.Uses)
        if (!known.contains(use.Text) && !known.contains(use.Text + ".other")) report.Missing.push_back(use);
    for (const std::string& key : english)
        if (!scan.Literals.contains(key) && !scan.Literals.contains(Parent(key))) report.Unused.push_back(key);
    report.RawText = scan.RawText;
    report.Stale   = FindStale(known);

    for (const Loc::Locale& locale : Loc::GetLocales())
        report.Languages.push_back({ .Code       = locale.Code,
                                     .Name       = locale.Name,
                                     .Translated = english.size() - Loc::GetMissing(locale.Code).size(),
                                     .Total      = english.size() });
    return report;
}

std::string Template()
{
    nlohmann::ordered_json out;
    out["$locale"] = { { "name", "" } };
    for (const auto& [key, text] : Loc::GetStrings(Loc::English)) out[key] = text;
    return out.dump(2) + "\n";
}
} // namespace Arcbit::LocCheck
