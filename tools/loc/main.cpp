// arcbit-loc - a game's localization against its content.
//
//   arcbit-loc check    --locales <dir>... --scan <path>... [--dialogs <dir>...] [--items <dir>...] [--data <file>...]
//       keys the content uses that English lacks and errors in dialog and
//       item files (exit code 1 if any), their warnings, raw text in layouts,
//       translations English no longer has, English keys nothing uses, and
//       each language's coverage
//   arcbit-loc coverage --locales <dir>... [--dialogs <dir>...] [--items <dir>...]
//       how much of English each language translates
//   arcbit-loc template --locales <dir>... [--dialogs <dir>...] [--items <dir>...] --out <file>
//       a new language's starting file: every English key and its text
//
// Dialog files (--dialogs), item files (--items) and the stats and party
// files (--data) bring their own English, so their lines and names count
// everywhere a locale file's would.
//
// The demo: arcbit-loc check --locales engine/assets/locale game/assets/locale
//                            --scan engine/src engine/assets game/src game/assets
//                            --dialogs game/assets/dialog --items game/assets/items

#include <arcbit/core/Loc.h>
#include <arcbit/core/LocCheck.h>
#include <arcbit/core/Log.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Paths = std::vector<std::filesystem::path>;

struct Options
{
    std::string                  Command;
    std::map<std::string, Paths> Lists; // --locales, --scan, --out: the values after each
};

const Paths& List(const Options& options, const std::string& name)
{
    static const Paths none;
    const auto         it = options.Lists.find(name);
    return it != options.Lists.end() ? it->second : none;
}

Options Parse(const int argc, char* argv[])
{
    Options options;
    if (argc > 1) options.Command = argv[1];
    Paths* list = nullptr;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.starts_with("--")) list = &options.Lists[arg];
        else if (list) list->emplace_back(arg);
    }
    return options;
}

void PrintCoverage(const LocCheck::Report& report)
{
    std::printf("\nCoverage\n");
    for (const auto& language : report.Languages) {
        const double percent = language.Total ? 100.0 * static_cast<double>(language.Translated) / static_cast<double>(language.Total) : 100.0;
        std::printf("  %-8s %5.1f%%  %zu / %zu  %s\n", language.Code.c_str(), percent, language.Translated, language.Total,
                    language.Name.c_str());
    }
}

void PrintFindings(const LocCheck::Report& report)
{
    std::printf("Missing from English (%zu)\n", report.Missing.size());
    for (const auto& use : report.Missing) std::printf("  %s  (%s:%u)\n", use.Text.c_str(), use.File.c_str(), use.Line);
    std::printf("\nDialog files (%zu)\n", report.Dialogs.size());
    for (const auto& problem : report.Dialogs)
        std::printf("  %s %s: %s  (%s)\n", problem.Error ? "error  " : "warning", problem.Dialog.c_str(), problem.Message.c_str(),
                    problem.File.c_str());
    std::printf("\nItem files (%zu)\n", report.Items.size());
    for (const auto& issue : report.Items)
        std::printf("  %s %s: %s  (%s)\n", issue.Error ? "error  " : "warning", issue.Item.c_str(), issue.Message.c_str(),
                    issue.File.c_str());
    std::printf("\nRaw text in layouts (%zu)\n", report.RawText.size());
    for (const auto& raw : report.RawText) std::printf("  \"%s\"  (%s:%u)\n", raw.Text.c_str(), raw.File.c_str(), raw.Line);
    std::printf("\nTranslated, but English has no such key - moved or removed? (%zu)\n", report.Stale.size());
    for (const auto& stale : report.Stale) std::printf("  %-8s %s\n", stale.Code.c_str(), stale.Key.c_str());
    std::printf("\nPossibly unused - keys built at run time can't be seen (%zu)\n", report.Unused.size());
    for (const auto& key : report.Unused) std::printf("  %s\n", key.c_str());
}

int RunCheck(const Options& options)
{
    const auto report = LocCheck::Check(List(options, "--locales"), List(options, "--scan"), List(options, "--dialogs"),
                                        List(options, "--items"), List(options, "--data"));
    const bool check  = options.Command == "check";
    if (check) PrintFindings(report);
    PrintCoverage(report);
    return check && (!report.Missing.empty() || report.HasDialogErrors() || report.HasItemErrors()) ? 1 : 0;
}

int RunTemplate(const Options& options)
{
    const Paths& out = List(options, "--out");
    if (out.size() != 1) {
        std::fprintf(stderr, "template needs --out <file>\n");
        return 2;
    }
    if (std::filesystem::exists(out[0])) {
        std::fprintf(stderr, "%s exists - not overwriting a translation\n", out[0].string().c_str());
        return 2;
    }
    (void)LocCheck::Check(List(options, "--locales"), {}, List(options, "--dialogs"), List(options, "--items"), List(options, "--data"));
    std::ofstream(out[0], std::ios::binary) << LocCheck::Template();
    std::printf("wrote %s\n", out[0].string().c_str());
    return 0;
}
} // namespace

int main(const int argc, char* argv[])
{
    Log::Init();
    Log::SetAllLevels(spdlog::level::warn); // the report is the output, not the engine's info lines
    const Options options = Parse(argc, argv);
    int           result  = 2;
    if (options.Command == "check" || options.Command == "coverage") result = RunCheck(options);
    else if (options.Command == "template") result = RunTemplate(options);
    else std::fprintf(stderr, "usage: arcbit-loc check | coverage | template  (see tools/loc/main.cpp)\n");
    Log::Shutdown();
    return result;
}
