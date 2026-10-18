#include <arcbit/core/LocCheck.h>
#include <arcbit/core/Loc.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace Arcbit::LocCheck
{
namespace
{
// The patterns that use a key, by file type; the key is the first group.
const std::vector<std::regex>& UsePatterns(const std::string& extension)
{
    static const std::vector<std::regex> json = { std::regex(R"re("\w+_key"\s*:\s*"([^"]+)")re") };
    static const std::vector<std::regex> lua  = { std::regex(R"re(Loc\.(?:Get|Format|Has)\(\s*["']([^"']+)["'])re"),
                                                  std::regex(R"re(TextKey\s*=\s*["']([^"']+)["'])re") };
    static const std::vector<std::regex> cpp  = { std::regex(R"re(Loc::(?:Get|Format|Has)\(\s*"([^"]+)")re"),
                                                  std::regex(R"re(Set(?:Text|Label)Key\(\s*"([^"]+)")re") };
    static const std::vector<std::regex> none;
    if (extension == ".arcui" || extension == ".arcscene") return json;
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

// A layout's "text" / "label" with words in it: player text that skipped the
// locale files ("<", "50%" and "..." don't count).
const std::regex& RawTextPattern()
{
    static const std::regex raw(R"re("(?:text|label)"\s*:\s*"([^"]*[A-Za-z]{2}[^"]*)")re");
    return raw;
}

struct Scan
{
    std::vector<Place>    Uses;
    std::set<std::string> Literals;
    std::vector<Place>    RawText;
};

void ScanFile(const std::filesystem::path& file, Scan& scan)
{
    const std::string extension = file.extension().string();
    const auto&       patterns  = UsePatterns(extension);
    if (patterns.empty()) return;
    const bool    layout = extension == ".arcui";
    std::ifstream in(file);
    std::string   line;
    for (u32 number = 1; std::getline(in, line); ++number) {
        for (const std::regex& pattern : patterns)
            for (std::sregex_iterator it(line.begin(), line.end(), pattern), end; it != end; ++it) {
                scan.Uses.push_back({ (*it)[1].str(), file.generic_string(), number });
                scan.Literals.insert((*it)[1].str()); // a used key needn't be dotted ("coins")
            }
        for (std::sregex_iterator it(line.begin(), line.end(), DottedLiteral()), end; it != end; ++it)
            scan.Literals.insert((*it)[1].str());
        if (layout)
            for (std::sregex_iterator it(line.begin(), line.end(), RawTextPattern()), end; it != end; ++it)
                scan.RawText.push_back({ (*it)[1].str(), file.generic_string(), number });
    }
}

Scan ScanRoots(const std::span<const std::filesystem::path> roots)
{
    Scan            scan;
    std::error_code ec;
    for (const auto& root : roots) {
        if (std::filesystem::is_regular_file(root, ec)) {
            ScanFile(root, scan);
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != decltype(it){}; it.increment(ec))
            if (it->is_regular_file()) ScanFile(it->path(), scan);
    }
    return scan;
}

// "item.coins.one" -> "item.coins": a plural form is used through its key.
std::string PluralParent(const std::string& key)
{
    static const std::set<std::string> forms = { "zero", "one", "two", "few", "many", "other" };
    const auto dot = key.rfind('.');
    return dot != std::string::npos && forms.contains(key.substr(dot + 1)) ? key.substr(0, dot) : key;
}

std::vector<std::string> EnglishKeys()
{
    std::vector<std::string> keys;
    for (auto& [key, text] : Loc::GetStrings(Loc::English)) keys.push_back(std::move(key));
    return keys;
}
} // namespace

Report Check(const std::span<const std::filesystem::path> localeFolders,
             const std::span<const std::filesystem::path> scanRoots)
{
    Loc::Clear();
    for (const auto& folder : localeFolders) Loc::AddFolder(folder);
    const std::vector<std::string> english = EnglishKeys();
    const std::set<std::string>    known(english.begin(), english.end());
    const Scan                     scan = ScanRoots(scanRoots);

    Report report;
    for (const Place& use : scan.Uses)
        if (!known.contains(use.Text) && !known.contains(use.Text + ".other")) report.Missing.push_back(use);
    for (const std::string& key : english)
        if (!scan.Literals.contains(key) && !scan.Literals.contains(PluralParent(key))) report.Unused.push_back(key);
    report.RawText = scan.RawText;

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
