#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace Arcbit
{
namespace
{
using Table = std::unordered_map<std::string, std::string>;

struct Language
{
    Loc::Locale                                  Info;
    std::vector<std::filesystem::path>           Files;   // read in order: later files' strings win
    std::vector<std::filesystem::file_time_type> Stamps;  // each file's write time when last read
    Table                                        Strings; // all its files, once loaded
    bool                                         Loaded = false;
    bool                                         Pseudo = false; // made from English, not files (qps)
};

// A language a player can pick: one with files, or the pseudo-locale.
bool Available(const Language& language) { return !language.Files.empty() || language.Pseudo; }

std::filesystem::file_time_type StampOf(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto      stamp = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : stamp;
}

struct State
{
    std::map<std::string, Language> Languages;  // by normalized code
    std::string                     Active{ Loc::English };
    std::vector<const Language*>    Chain;      // looked through in order by Get
    Table                           Echo;       // missing keys, returned as themselves
    std::unordered_set<std::string> Warned;     // missing keys already logged
    u64                             Generation = 1;
};

State& S()
{
    static State state;
    return state;
}

// "pt_BR", "PT-br" and "pt-BR" are the same language.
std::string Normalize(const std::string_view code)
{
    std::string out(code);
    for (char& c : out) c = c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<nlohmann::json> ReadJson(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN(Engine, "Loc: cannot open '{}'", path.string());
        return std::nullopt;
    }
    try {
        nlohmann::json root = nlohmann::json::parse(file);
        if (root.is_object()) return root;
        LOG_ERROR(Engine, "Loc: '{}' isn't a JSON object", path.string());
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Engine, "Loc: parse error in '{}': {}", path.string(), e.what());
    }
    return std::nullopt;
}

// Nested objects join their keys with dots; values that aren't strings are skipped.
void Flatten(const nlohmann::json& object, const std::string& prefix, Table& out)
{
    for (const auto& [key, value] : object.items()) {
        if (prefix.empty() && key == "$locale") continue;
        const std::string full = prefix.empty() ? key : prefix + "." + key;
        if (value.is_string()) out[full] = value.get<std::string>();
        else if (value.is_object()) Flatten(value, full, out);
    }
}

// The "$locale" block: the name to show, and fonts relative to the file.
void ReadMeta(const nlohmann::json& root, const std::filesystem::path& file, Loc::Locale& info)
{
    const auto it = root.find("$locale");
    if (it == root.end() || !it->is_object()) return;
    if (const auto name = it->find("name"); name != it->end() && name->is_string()) info.Name = name->get<std::string>();
    if (const auto fonts = it->find("fonts"); fonts != it->end() && fonts->is_array())
        for (const auto& font : *fonts)
            if (font.is_string()) {
                const std::string path = (file.parent_path() / font.get<std::string>()).lexically_normal().generic_string();
                if (std::ranges::find(info.Fonts, path) == info.Fonts.end()) info.Fonts.push_back(path);
            }
}

Language* Find(std::string_view code);

void LoadStrings(Language& language)
{
    if (language.Pseudo) { // from English, loaded first
        Language* english = Find(Loc::English);
        if (english && !english->Loaded) LoadStrings(*english);
        language.Strings.clear();
        if (english)
            for (const auto& [key, text] : english->Strings) // format settings ("loc.decimal") stay as they are
                language.Strings[key] = key.starts_with("loc.") ? text : Loc::Pseudo(text);
        language.Loaded = true;
        ++S().Generation;
        return;
    }
    language.Strings.clear();
    language.Stamps.clear();
    for (const auto& file : language.Files) {
        language.Stamps.push_back(StampOf(file));
        if (const auto root = ReadJson(file)) {
            Flatten(*root, "", language.Strings);
            ReadMeta(*root, file, language.Info);
        }
    }
    language.Loaded = true;
    ++S().Generation;
}

Language* Find(const std::string_view code)
{
    const auto it = S().Languages.find(Normalize(code));
    return it != S().Languages.end() ? &it->second : nullptr;
}

// The active language, the one it's a region of (pt-BR -> pt), then English.
void RebuildChain()
{
    State& s = S();
    s.Chain.clear();
    const auto add = [&s](const std::string_view code) {
        Language* language = Find(code);
        if (!language || std::ranges::find(s.Chain, language) != s.Chain.end()) return;
        if (!language->Loaded) LoadStrings(*language);
        s.Chain.push_back(language);
    };
    add(s.Active);
    if (const auto dash = s.Active.find('-'); dash != std::string::npos) add(std::string_view(s.Active).substr(0, dash));
    add(Loc::English);
}

// What a missing key is logged for: things shaped like keys ("ui.ok"), not
// plain text passed through Get by a half-localized game.
[[maybe_unused]] bool LooksLikeKey(const std::string_view text)
{
    return text.find('.') != std::string_view::npos && text.find(' ') == std::string_view::npos;
}
} // namespace

void Loc::AddFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    std::ranges::sort(files); // the same order on every machine

    for (const auto& file : files) {
        const std::string code     = file.stem().string();
        Language&         language = S().Languages[Normalize(code)];
        if (language.Info.Code.empty()) language.Info = { .Code = code, .Name = code };
        language.Files.push_back(file);
        if (const auto root = ReadJson(file)) ReadMeta(*root, file, language.Info);
        if (language.Loaded) LoadStrings(language); // already in use: take the new file's strings now
    }
    RebuildChain();
    LOG_INFO(Engine, "Loc: {} locale file(s) in '{}'", files.size(), folder.string());
}

std::vector<Loc::Locale> Loc::GetLocales()
{
    std::vector<Locale> out;
    for (const auto& language : S().Languages | std::views::values)
        if (Available(language)) out.push_back(language.Info);
    std::ranges::stable_partition(out, [](const Locale& l) { return Normalize(l.Code) == English; });
    return out;
}

const Loc::Locale* Loc::FindLocale(const std::string_view code)
{
    const Language* language = Find(code);
    return language && Available(*language) ? &language->Info : nullptr;
}

std::optional<std::string> Loc::Match(const std::span<const std::string> preferred)
{
    for (const std::string& code : preferred) {
        if (const Locale* exact = FindLocale(code)) return exact->Code;
        const std::string language = Normalize(code).substr(0, Normalize(code).find('-'));
        if (const Locale* base = FindLocale(language)) return base->Code;
    }
    return std::nullopt;
}

bool Loc::SetLanguage(const std::string_view code)
{
    const Language* language = Find(code);
    if (!language || !Available(*language)) {
        LOG_WARN(Engine, "Loc: no locale files for '{}'", code);
        return false;
    }
    S().Active = language->Info.Code;
    RebuildChain();
    ++S().Generation;
    LOG_INFO(Engine, "Loc: language is '{}' ({})", language->Info.Code, language->Info.Name);
    return true;
}

const std::string& Loc::GetLanguage() { return S().Active; }

u64 Loc::GetGeneration() { return S().Generation; }

bool Loc::Load(const std::string_view path)
{
    const std::filesystem::path file(path);
    const auto                  root = ReadJson(file);
    if (!root) return false;

    // Kept with the language's files, so a reload reads it again.
    Language& language = S().Languages[Normalize(S().Active)];
    if (language.Info.Code.empty()) language.Info = { .Code = S().Active, .Name = S().Active };
    language.Files.push_back(file);
    ReadMeta(*root, file, language.Info);
    if (language.Loaded) {
        Flatten(*root, "", language.Strings);
        language.Stamps.push_back(StampOf(file));
    }
    else LoadStrings(language);
    ++S().Generation;
    RebuildChain();
    LOG_INFO(Engine, "Loc: loaded '{}' into '{}'", file.string(), S().Active);
    return true;
}

const std::string& Loc::Get(const std::string_view key)
{
    State&            s = S();
    const std::string k(key);
    for (const Language* language : s.Chain)
        if (const auto it = language->Strings.find(k); it != language->Strings.end()) return it->second;

#ifdef ARCBIT_DEBUG
    if (LooksLikeKey(key) && s.Warned.insert(k).second) LOG_WARN(Engine, "Loc: no string for '{}' in '{}'", key, s.Active);
#endif
    return s.Echo.try_emplace(k, k).first->second;
}

bool Loc::Has(const std::string_view key)
{
    const std::string k(key);
    return std::ranges::any_of(S().Chain, [&k](const Language* l) { return l->Strings.contains(k); });
}

std::string Loc::Format(const std::string_view key, const std::initializer_list<LocArg> args)
{
    return Format(key, std::span(args.begin(), args.size()));
}

// With a "count", each language in the chain that has plural forms of the
// key is asked in turn, by its own rules; else the key's plain string.
std::string Loc::Format(const std::string_view key, const std::span<const LocArg> args)
{
    const auto count = std::ranges::find(args, std::string_view("count"), &LocArg::Name);
    if (count != args.end() && count->Number)
        for (const Language* language : S().Chain) {
            const std::string_view form = PluralForm(language->Info.Code, *count->Number);
            for (const std::string_view f : { form, std::string_view("other") }) {
                const auto it = language->Strings.find(std::format("{}.{}", key, f));
                if (it != language->Strings.end()) return Interpolate(it->second, args);
            }
        }
    return Interpolate(Get(key), args);
}

std::string Loc::Interpolate(const std::string_view text, const std::span<const LocArg> args)
{
    std::string out;
    out.reserve(text.size());
    for (usize i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) { // {{ or }}
            out += c;
            ++i;
            continue;
        }
        const usize close = c == '{' ? text.find('}', i + 1) : std::string_view::npos;
        if (close == std::string_view::npos) {
            out += c;
            continue;
        }
        const std::string_view inside = text.substr(i + 1, close - i - 1);
        const usize            colon  = inside.find(':');
        const std::string_view name   = inside.substr(0, colon);
        const std::string_view spec   = colon == std::string_view::npos ? std::string_view{} : inside.substr(colon + 1);
        const auto             arg    = std::ranges::find(args, name, &LocArg::Name);
        if (arg != args.end()) out += FormatArg(*arg, spec);
        else out += text.substr(i, close - i + 1);
        i = close;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> Loc::GetStrings(const std::string_view code)
{
    Language* language = Find(code);
    if (!language || !Available(*language)) return {};
    if (!language->Loaded) LoadStrings(*language);
    std::vector<std::pair<std::string, std::string>> out(language->Strings.begin(), language->Strings.end());
    std::ranges::sort(out);
    return out;
}

std::vector<std::string> Loc::GetMissing(const std::string_view code)
{
    const std::string wanted = code.empty() ? S().Active : std::string(code);
    Language*         target = Find(wanted);
    const Language*   base   = Find(Normalize(wanted).substr(0, Normalize(wanted).find('-')));
    const Language*   en     = Find(English);
    if (!target || !en) return {};
    if (!target->Loaded) LoadStrings(*target);

    std::vector<std::string> missing;
    for (const auto& key : en->Strings | std::views::keys)
        if (!target->Strings.contains(key) && !(base && base != target && base->Strings.contains(key))) missing.push_back(key);
    std::ranges::sort(missing);
    return missing;
}

u32 Loc::CheckReloads()
{
    u32 reloaded = 0;
    for (auto& [code, language] : S().Languages) {
        if (!language.Loaded || language.Pseudo) continue;
        bool changed = language.Stamps.size() != language.Files.size();
        for (usize i = 0; !changed && i < language.Files.size(); ++i) changed = StampOf(language.Files[i]) != language.Stamps[i];
        if (!changed) continue;
        LoadStrings(language);
        LOG_INFO(Engine, "Loc: reloaded '{}'", language.Info.Code);
        ++reloaded;
    }
    if (reloaded > 0) // English may have changed: the pseudo-locale follows
        for (auto& language : S().Languages | std::views::values)
            if (language.Pseudo && language.Loaded) LoadStrings(language);
    return reloaded;
}

void Loc::AddPseudoLocale()
{
    Language& language = S().Languages[Normalize(PseudoCode)];
    language.Info      = { .Code = std::string(PseudoCode), .Name = "[Pseudo]" };
    language.Pseudo    = true;
    language.Loaded    = false;
}

void Loc::ForEachString(const std::function<void(std::string_view)>& visit)
{
    for (const Language* language : S().Chain)
        for (const std::string& text : language->Strings | std::views::values) visit(text);
}

void Loc::Clear() { S() = State{}; }

} // namespace Arcbit
