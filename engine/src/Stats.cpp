#include <arcbit/items/Stats.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/items/Item.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace Arcbit
{
namespace
{
StatDef Make(std::string id, std::string name, const f32 value, std::string maxStat = {})
{
    StatDef def{ .Id = std::move(id), .Name = std::move(name), .Default = value, .MaxStat = std::move(maxStat) };
    def.NameKey = std::format("stats.{}.name", def.Id);
    return def;
}
} // namespace

StatSet::StatSet()
{
    _stats = { Make("HP", "HP", 100, "MaxHP"),   Make("MaxHP", "Max HP", 100), Make("MP", "MP", 20, "MaxMP"),
               Make("MaxMP", "Max MP", 20),       Make("ATK", "Attack", 10),    Make("DEF", "Defense", 10),
               Make("MAG", "Magic", 10),          Make("RES", "Resistance", 10), Make("SPD", "Speed", 10),
               Make("LUK", "Luck", 10) };
}

bool StatSet::LoadFile(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) return false;
    try {
        Load(nlohmann::json::parse(in, nullptr, true, true));
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "Stats: '{}' - {}", file.string(), e.what());
        return false;
    }
}

void StatSet::Load(const nlohmann::json& json)
{
    if (json.value("replace", false)) _stats.clear();
    for (const auto& [id, j] : json.value("stats", nlohmann::json::object()).items()) {
        const StatDef* was = Find(id);
        StatDef        def = was ? *was : Make(id, id, 0.0f);
        def.Name           = j.value("name", def.Name);
        def.Named          = def.Named || j.contains("name");
        def.Default        = j.value("default", def.Default);
        def.Min            = j.value("min", def.Min);
        def.MaxStat        = j.value("max_stat", def.MaxStat);
        if (j.contains("max")) def.Max = j["max"].is_null() ? std::nullopt : std::optional<f32>(j["max"].get<f32>());
        Upsert(std::move(def));
    }
    for (const StatDef& def : _stats)
        if (!def.MaxStat.empty() && !Find(def.MaxStat))
            LOG_WARN(Engine, "Stats: '{}' is capped by '{}', which isn't a stat", def.Id, def.MaxStat);
}

void StatSet::Upsert(StatDef def)
{
    const auto it = std::ranges::find(_stats, def.Id, &StatDef::Id);
    if (it != _stats.end()) *it = std::move(def);
    else _stats.push_back(std::move(def));
}

const StatDef* StatSet::Find(const std::string_view id) const
{
    const auto it = std::ranges::find(_stats, id, &StatDef::Id);
    return it != _stats.end() ? &*it : nullptr;
}

f32 StatSet::Clamp(const StatDef& def, const f32 value) const
{
    const f32 low = std::max(value, def.Min);
    return def.Max ? std::min(low, *def.Max) : low;
}

StatValues StatSet::Effective(const Stats& stats, const std::span<const ItemDef* const> equipped) const
{
    StatValues flat, percent;
    for (const ItemDef* item : equipped)
        for (const auto& [stat, amount] : item->Stats)
            (stat.ends_with('%') ? percent[stat.substr(0, stat.size() - 1)] : flat[stat]) += amount;

    StatValues out;
    for (const StatDef& def : _stats) {
        const auto base  = stats.Base.find(def.Id);
        f32        value = (base != stats.Base.end() ? base->second : def.Default);
        if (const auto f = flat.find(def.Id); f != flat.end()) value += f->second;
        if (const auto p = percent.find(def.Id); p != percent.end()) value *= 1.0f + p->second / 100.0f;
        out[def.Id] = Clamp(def, value);
    }
    for (const StatDef& def : _stats) { // resources: their current value, within their maximum
        if (def.MaxStat.empty()) continue;
        const f32  most    = out.contains(def.MaxStat) ? out[def.MaxStat] : out[def.Id];
        const auto current = stats.Current.find(def.Id);
        out[def.Id]        = std::clamp(current != stats.Current.end() ? current->second : most, def.Min, most);
    }
    return out;
}

void StatSet::Apply(Stats& stats, const StatValues& changes, const std::span<const ItemDef* const> equipped) const
{
    const StatValues now = Effective(stats, equipped);
    for (const auto& [id, change] : changes) {
        const StatDef* def = Find(id);
        if (!def) {
            LOG_WARN(Engine, "Stats: there's no stat '{}'", id);
            continue;
        }
        if (!def->MaxStat.empty()) {
            const f32 most     = now.contains(def->MaxStat) ? now.at(def->MaxStat) : now.at(id);
            stats.Current[id]  = std::clamp(now.at(id) + change, def->Min, most);
            continue;
        }
        const auto base = stats.Base.find(id);
        stats.Base[id]  = Clamp(*def, (base != stats.Base.end() ? base->second : def->Default) + change);
    }
}

void StatSet::PublishStrings() const
{
    std::vector<std::pair<std::string, std::string>> strings;
    for (const StatDef& def : _stats)
        if (def.Named) strings.emplace_back(def.NameKey, def.Name);
    Loc::SetStrings(Loc::English, "stats", strings);
}
} // namespace Arcbit
