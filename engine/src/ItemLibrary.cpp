#include <arcbit/items/ItemLibrary.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <ranges>
#include <set>

namespace Arcbit
{
namespace
{
using nlohmann::json;

constexpr std::string_view LocSource = "items"; // Loc's name for the items' strings

constexpr std::array KnownFields = { "name",  "description", "icon",        "type",       "category", "tags",
                                     "stack", "weight",      "value",       "size",       "rotatable", "slot",
                                     "stats", "use",         "drop_prefab", "properties", "extends",  "abstract" };

std::filesystem::file_time_type StampOf(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto      stamp = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : stamp;
}

// Objects merge field by field; anything else is replaced; null removes.
void MergeInto(json& base, const json& over)
{
    for (const auto& [key, value] : over.items()) {
        if (value.is_null()) base.erase(key);
        else if (value.is_object() && base.contains(key) && base[key].is_object()) MergeInto(base[key], value);
        else base[key] = value;
    }
}
} // namespace

bool ItemDef::HasTag(const std::string_view tag) const { return std::ranges::find(Tags, tag) != Tags.end(); }

u32 ItemLibrary::AddFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return 0;
    const usize before = _items.size();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != Extension) continue;
        File& file = _files.emplace_back();
        file.Path  = entry.path();
        ReadFile(file);
    }
    Build();
    const u32 added = static_cast<u32>(_items.size() > before ? _items.size() - before : 0);
    LOG_INFO(Engine, "Items: {} item(s) in '{}'", added, folder.string());
    return added;
}

// A file that doesn't parse holds nothing until it does.
void ItemLibrary::ReadFile(File& file)
{
    file.Stamp = StampOf(file.Path);
    file.Items = nlohmann::ordered_json::object();
    file.ParseError.clear();
    std::ifstream in(file.Path);
    try {
        auto items = nlohmann::ordered_json::parse(in, nullptr, true, true); // comments allowed
        if (!items.is_object()) throw std::runtime_error("an .arcitems file must be a JSON object of id: item");
        file.Items = std::move(items);
    }
    catch (const std::exception& e) {
        file.ParseError = e.what();
    }
}

// Every item from scratch: "extends" can reach into any file.
void ItemLibrary::Build()
{
    _items.clear();
    _itemFiles.clear();
    _issues.clear();
    const std::map<std::string, Source> sources = Gather();
    for (const auto& [id, source] : sources) {
        if (source.Json->value("abstract", false)) continue; // only for extending
        const std::optional<json> item = Resolve(id, sources);
        if (!item) continue;
        try {
            _items[id] = Parse(id, *item, *source.From);
            _itemFiles[id] = source.From->Path;
        }
        catch (const std::exception& e) {
            Issue(id, source.From, true, std::format("malformed - {}", e.what()));
        }
    }
    PublishStrings();
    for (const ItemIssue& issue : _issues) {
        if (!LogIssues) break;
        if (issue.Error) LOG_ERROR(Engine, "Item '{}' ({}): {}", issue.Item, issue.File, issue.Message);
        else LOG_WARN(Engine, "Item '{}' ({}): {}", issue.Item, issue.File, issue.Message);
    }
}

// Every item as written, by id; a second item with an id is an error (the first stays).
std::map<std::string, ItemLibrary::Source> ItemLibrary::Gather()
{
    std::map<std::string, Source> sources;
    for (const File& file : _files) {
        if (!file.ParseError.empty()) Issue("", &file, true, std::format("can't read - {}", file.ParseError));
        for (const auto& [id, item] : file.Items.items()) {
            if (!item.is_object()) Issue(id, &file, true, "an item must be a JSON object");
            else if (const auto taken = sources.find(id); taken != sources.end())
                Issue(id, &file, true, std::format("the id is taken (in {})", taken->second.From->Path.generic_string()));
            else sources[id] = { &item, &file };
        }
    }
    return sources;
}

// The item with every "extends" merged in, nearest last.
std::optional<json> ItemLibrary::Resolve(const std::string& id, const std::map<std::string, Source>& sources)
{
    std::vector<const nlohmann::ordered_json*> chain;
    std::set<std::string>                      seen;
    for (std::string next = id; !next.empty();) {
        const auto it = sources.find(next);
        if (it == sources.end() || !seen.insert(next).second) {
            Issue(id, sources.at(id).From, true,
                  it == sources.end() ? std::format("extends '{}', which isn't an item", next)
                                      : std::format("extends itself (through '{}')", next));
            return std::nullopt;
        }
        chain.push_back(it->second.Json);
        next = it->second.Json->value("extends", std::string{});
    }
    json item = json::object();
    for (const nlohmann::ordered_json* step : chain | std::views::reverse) MergeInto(item, json(*step));
    item.erase("extends");
    item.erase("abstract");
    return item;
}

ItemDef ItemLibrary::Parse(const std::string& id, const json& item, const File& file)
{
    for (const auto& [field, value] : item.items())
        if (std::ranges::find(KnownFields, field) == KnownFields.end())
            Issue(id, &file, false, std::format("unknown field '{}'", field));
    ItemDef def{ .Id             = id,
                 .Name           = item.value("name", std::string{}),
                 .Description    = item.value("description", std::string{}),
                 .NameKey        = std::format("items.{}.name", id),
                 .DescriptionKey = std::format("items.{}.description", id),
                 .Icon           = item.value("icon", std::string{}),
                 .Type           = item.value("type", std::string(ItemTypes::Material)) };
    if (def.Name.empty()) Issue(id, &file, true, "it needs a \"name\"");
    def.Category   = item.value("category", def.Type);
    def.Tags       = item.value("tags", std::vector<std::string>{});
    def.Weight     = item.value("weight", 0.0f);
    def.Value      = item.value("value", i64{ 0 });
    def.DropPrefab = item.value("drop_prefab", std::string{});
    def.Use        = item.value("use", json::object());
    def.Properties = item.value("properties", json::object());
    const auto byType = ItemRules.StackByType.find(def.Type);
    const i64  stack  = item.value("stack", static_cast<i64>(byType != ItemRules.StackByType.end() ? byType->second : ItemRules.Stack));
    def.Stack         = static_cast<u32>(std::max<i64>(stack, 1));
    if (stack < 1) Issue(id, &file, true, "\"stack\" must be 1 or more");
    if (def.Weight < 0.0f) {
        Issue(id, &file, true, "\"weight\" can't be negative");
        def.Weight = 0.0f;
    }
    ReadGrid(def, item, file);
    ReadEquipment(def, item, file);
    CheckIcon(def, file);
    return def;
}

void ItemLibrary::ReadGrid(ItemDef& def, const json& item, const File& file)
{
    def.Rotatable = item.value("rotatable", true);
    const auto size = item.find("size");
    if (size == item.end()) return;
    if (!size->is_array() || size->size() != 2 || !(*size)[0].is_number_unsigned() || !(*size)[1].is_number_unsigned() ||
        (*size)[0].get<u32>() < 1 || (*size)[1].get<u32>() < 1)
        return Issue(def.Id, &file, true, "\"size\" must be [width, height], each 1 or more");
    def.Width  = (*size)[0].get<u32>();
    def.Height = (*size)[1].get<u32>();
}

void ItemLibrary::ReadEquipment(ItemDef& def, const json& item, const File& file)
{
    def.Slot = item.value("slot", std::string{});
    for (const auto& [stat, value] : item.value("stats", json::object()).items()) {
        if (value.is_number()) def.Stats[stat] = value.get<f32>();
        else Issue(def.Id, &file, true, std::format("stat '{}' must be a number", stat));
    }
    const bool equipment = def.Type == ItemTypes::Equipment;
    if (equipment && def.Slot.empty()) Issue(def.Id, &file, false, "equipment without a \"slot\" can't be worn");
    if (!equipment && !def.Slot.empty()) Issue(def.Id, &file, false, "a \"slot\" on an item that isn't equipment");
    const auto& kinds = ItemRules.SlotKinds;
    if (!def.Slot.empty() && !kinds.empty() && std::ranges::find(kinds, def.Slot) == kinds.end())
        Issue(def.Id, &file, false, std::format("no equipment slot takes '{}'", def.Slot));
}

void ItemLibrary::CheckIcon(const ItemDef& def, const File& file)
{
    if (!ItemRules.AssetRoot || def.Icon.empty()) return;
    const std::string path = def.Icon.substr(0, def.Icon.find('#')); // "sheet.json#frame": the sheet
    std::error_code   ec;
    if (!std::filesystem::exists(*ItemRules.AssetRoot / path, ec))
        Issue(def.Id, &file, false, std::format("icon '{}' not found", path));
}

void ItemLibrary::PublishStrings() const
{
    std::vector<std::pair<std::string, std::string>> strings;
    for (const ItemDef& item : _items | std::views::values) {
        if (!item.Name.empty()) strings.emplace_back(item.NameKey, item.Name); // a nameless one is reported
        if (!item.Description.empty()) strings.emplace_back(item.DescriptionKey, item.Description);
    }
    Loc::SetStrings(Loc::English, std::string(LocSource), strings);
}

const ItemDef* ItemLibrary::Find(const std::string_view id) const
{
    const auto it = _items.find(id);
    return it != _items.end() ? &it->second : nullptr;
}

std::vector<std::string> ItemLibrary::GetIds() const
{
    std::vector<std::string> ids;
    for (const std::string& id : _items | std::views::keys) ids.push_back(id);
    return ids;
}

std::filesystem::path ItemLibrary::GetFile(const std::string_view id) const
{
    const auto it = _itemFiles.find(id);
    return it != _itemFiles.end() ? it->second : std::filesystem::path{};
}

u32 ItemLibrary::CheckReloads()
{
    u32 changed = 0;
    for (File& file : _files) {
        if (StampOf(file.Path) == file.Stamp) continue;
        ReadFile(file);
        LOG_INFO(Engine, "Items: reloaded '{}'", file.Path.generic_string());
        ++changed;
    }
    if (changed) Build();
    return changed;
}

void ItemLibrary::Clear()
{
    _files.clear();
    _items.clear();
    _itemFiles.clear();
    _issues.clear();
    Loc::SetStrings(Loc::English, std::string(LocSource), {});
}

void ItemLibrary::Issue(std::string item, const File* file, const bool error, std::string message)
{
    _issues.push_back({ std::move(item), file ? file->Path.generic_string() : std::string{}, error, std::move(message) });
}
} // namespace Arcbit
