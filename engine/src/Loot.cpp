#include <arcbit/items/Loot.h>

#include <arcbit/core/Log.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <ranges>

namespace Arcbit
{
namespace
{
using nlohmann::json;

// A table deeper than this inside others is a loop Check missed (a table
// added after it ran); the roll stops there.
constexpr u32 MaxDepth = 16;

constexpr std::array TableFields = { "rolls", "always", "entries" };
constexpr std::array EntryFields = { "item", "currency", "table", "count", "weight" };

template <size_t N>
bool Known(const std::array<const char*, N>& fields, const std::string& key)
{
    return std::ranges::find(fields, key) != fields.end();
}

u32 Pick(const u32 min, const u32 max, std::mt19937& rng)
{
    return min >= max ? min : std::uniform_int_distribution<u32>(min, max)(rng);
}
} // namespace

u32 LootLibrary::AddFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return 0;
    u32 added = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != Extension) continue;
        const std::string file = entry.path().generic_string();
        std::ifstream     in(entry.path());
        try {
            const json tables = json::parse(in, nullptr, true, true); // comments allowed
            if (!tables.is_object()) throw std::runtime_error("an .arcloot file must be a JSON object of id: table");
            for (const auto& [id, table] : tables.items()) {
                if (_files.contains(id)) Issue(id, file, true, std::format("also in {} - this one is ignored", _files.find(id)->second));
                else {
                    _tables.insert_or_assign(id, Parse(id, table, file));
                    _files.emplace(id, file);
                    ++added;
                }
            }
        }
        catch (const std::exception& e) {
            Issue({}, file, true, e.what());
        }
    }
    LOG_INFO(Engine, "Loot: {} table(s) in '{}'", added, folder.string());
    return added;
}

void LootLibrary::Add(std::string id, const nlohmann::json& table)
{
    LootTable parsed = Parse(id, table, "<code>");
    _files.insert_or_assign(id, "<code>");
    _tables.insert_or_assign(std::move(id), std::move(parsed));
}

const LootTable* LootLibrary::Find(const std::string_view id) const
{
    const auto it = _tables.find(id);
    return it != _tables.end() ? &it->second : nullptr;
}

std::vector<std::string> LootLibrary::GetIds() const
{
    std::vector<std::string> ids;
    for (const auto& id : _tables | std::views::keys) ids.push_back(id);
    return ids;
}

LootTable LootLibrary::Parse(const std::string& id, const nlohmann::json& table, const std::string& file)
{
    LootTable parsed{ .Id = id };
    if (!table.is_object()) {
        Issue(id, file, true, "a table must be a JSON object");
        return parsed;
    }
    for (const auto& [key, value] : table.items())
        if (!Known(TableFields, key)) Issue(id, file, false, std::format("unknown field '{}'", key));
    if (table.contains("rolls")) ReadRange(table["rolls"], parsed.RollsMin, parsed.RollsMax, id, file);
    for (const json& entry : table.value("always", json::array())) parsed.Always.push_back(ParseEntry(id, entry, file));
    for (const json& entry : table.value("entries", json::array())) parsed.Entries.push_back(ParseEntry(id, entry, file));
    if (parsed.RollsMax > 0 && !parsed.Entries.empty() && std::ranges::all_of(parsed.Entries, [](const LootEntry& e) { return e.Weight == 0; }))
        Issue(id, file, false, "every entry's weight is 0: its rolls give nothing");
    return parsed;
}

LootEntry LootLibrary::ParseEntry(const std::string& table, const nlohmann::json& entry, const std::string& file)
{
    LootEntry parsed;
    if (!entry.is_object()) {
        Issue(table, file, true, "an entry must be a JSON object");
        return parsed;
    }
    for (const auto& [key, value] : entry.items())
        if (!Known(EntryFields, key)) Issue(table, file, false, std::format("unknown entry field '{}'", key));
    constexpr std::array<std::pair<const char*, LootKind>, 3> Kinds = {
        { { "item", LootKind::Item }, { "currency", LootKind::Currency }, { "table", LootKind::Table } } };
    for (const auto& [key, kind] : Kinds) {
        if (!entry.contains(key)) continue;
        if (parsed.Kind != LootKind::Nothing) Issue(table, file, true, "an entry gives one item, currency or table");
        else if (!entry[key].is_string()) Issue(table, file, true, std::format("\"{}\" must be an id", key));
        else {
            parsed.Kind = kind;
            parsed.Id   = entry[key].get<std::string>();
        }
    }
    if (entry.contains("count")) ReadRange(entry["count"], parsed.Min, parsed.Max, table, file);
    if (const auto w = entry.find("weight"); w != entry.end()) {
        if (w->is_number_unsigned()) parsed.Weight = w->get<u32>();
        else Issue(table, file, true, "\"weight\" must be a whole number, 0 or more");
    }
    return parsed;
}

// A number, or [min, max].
void LootLibrary::ReadRange(const nlohmann::json& value, u32& min, u32& max, const std::string& table, const std::string& file)
{
    if (value.is_number_unsigned()) min = max = value.get<u32>();
    else if (value.is_array() && value.size() == 2 && value[0].is_number_unsigned() && value[1].is_number_unsigned()) {
        min = value[0].get<u32>();
        max = value[1].get<u32>();
        if (min > max) {
            Issue(table, file, true, std::format("[{}, {}] runs backwards", min, max));
            std::swap(min, max);
        }
    }
    else Issue(table, file, true, std::format("expected a whole number or [min, max], not {}", value.dump()));
}

void LootLibrary::Check(const std::function<bool(std::string_view)>& isItem,
                        const std::function<bool(std::string_view)>& isCurrency)
{
    for (const auto& [id, table] : _tables) {
        const std::string& file = _files.find(id)->second;
        for (const std::vector<LootEntry>* list : { &table.Always, &table.Entries })
            for (const LootEntry& entry : *list) {
                if (entry.Kind == LootKind::Item && !isItem(entry.Id)) Issue(id, file, true, std::format("no item '{}'", entry.Id));
                if (entry.Kind == LootKind::Currency && !isCurrency(entry.Id)) Issue(id, file, true, std::format("no currency '{}'", entry.Id));
                if (entry.Kind == LootKind::Table && !Find(entry.Id)) Issue(id, file, true, std::format("no table '{}'", entry.Id));
            }
        std::vector<std::string> path;
        if (Loops(id, path)) Issue(id, file, true, std::format("rolls itself: {}", path.front()));
    }
}

// Whether `id` reaches itself through its "table" entries; `path` is then
// "a -> b -> a".
bool LootLibrary::Loops(const std::string& id, std::vector<std::string>& path) const
{
    std::vector<std::pair<std::string, std::string>> stack = { { id, id } }; // table, the way there
    std::vector<std::string>                         seen;
    while (!stack.empty()) {
        const auto [at, way] = stack.back();
        stack.pop_back();
        const LootTable* table = Find(at);
        if (!table) continue;
        for (const std::vector<LootEntry>* list : { &table->Always, &table->Entries })
            for (const LootEntry& entry : *list) {
                if (entry.Kind != LootKind::Table) continue;
                if (entry.Id == id) {
                    path = { way + " -> " + id };
                    return true;
                }
                if (std::ranges::find(seen, entry.Id) != seen.end()) continue;
                seen.push_back(entry.Id);
                stack.emplace_back(entry.Id, way + " -> " + entry.Id);
            }
    }
    return false;
}

std::vector<LootStack> LootLibrary::Roll(const std::string_view id, std::mt19937& rng) const
{
    std::vector<LootStack> out;
    if (const LootTable* table = Find(id)) RollInto(*table, rng, out, 0);
    else LOG_WARN(Engine, "Loot: no table '{}'", id);
    return out;
}

void LootLibrary::RollInto(const LootTable& table, std::mt19937& rng, std::vector<LootStack>& out, const u32 depth) const
{
    if (depth > MaxDepth) return;
    for (const LootEntry& entry : table.Always) Give(entry, rng, out, depth);
    u32 total = 0;
    for (const LootEntry& entry : table.Entries) total += entry.Weight;
    if (total == 0) return;
    for (u32 roll = Pick(table.RollsMin, table.RollsMax, rng); roll > 0; --roll) {
        u32 at = std::uniform_int_distribution<u32>(0, total - 1)(rng);
        for (const LootEntry& entry : table.Entries) {
            if (at < entry.Weight) {
                Give(entry, rng, out, depth);
                break;
            }
            at -= entry.Weight;
        }
    }
}

void LootLibrary::Give(const LootEntry& entry, std::mt19937& rng, std::vector<LootStack>& out, const u32 depth) const
{
    if (entry.Kind == LootKind::Nothing) return;
    const u32 count = Pick(entry.Min, entry.Max, rng);
    if (entry.Kind == LootKind::Table) {
        if (const LootTable* table = Find(entry.Id))
            for (u32 i = 0; i < count; ++i) RollInto(*table, rng, out, depth + 1);
        return;
    }
    if (count == 0) return;
    const bool currency = entry.Kind == LootKind::Currency;
    const auto same     = std::ranges::find_if(out, [&](const LootStack& s) { return s.Id == entry.Id && s.Currency == currency; });
    if (same != out.end()) same->Count += count;
    else out.push_back({ entry.Id, count, currency });
}

void LootLibrary::Issue(std::string table, std::string file, const bool error, std::string message)
{
    if (LogIssues && error) LOG_ERROR(Engine, "Loot '{}' ({}): {}", table, file, message);
    else if (LogIssues) LOG_WARN(Engine, "Loot '{}' ({}): {}", table, file, message);
    _issues.push_back({ std::move(table), std::move(file), error, std::move(message) });
}
} // namespace Arcbit
