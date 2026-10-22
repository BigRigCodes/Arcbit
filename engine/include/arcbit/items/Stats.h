#pragma once

#include <arcbit/core/Types.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
struct ItemDef;

using StatValues = std::map<std::string, f32, std::less<>>;

// ---------------------------------------------------------------------------
// One stat a character has (docs/items.md#stats): HP, ATK, a game's own STR.
// A resource (HP, MP) has a current value that can't go past another stat,
// its maximum (MaxHP) - it's spent and restored; the others are the
// character's ability, changed by level-ups, potions of strength, and
// equipment.
// ---------------------------------------------------------------------------
struct StatDef
{
    std::string Id;
    std::string Name;        // English
    std::string NameKey;     // stats.<id>.name
    f32         Default = 0.0f;
    f32         Min     = 0.0f;
    std::optional<f32> Max;  // none: no limit
    std::string MaxStat;     // a resource: the stat its current value can't pass
    bool        Named = false; // its name came from a game's stats file (the engine's are in its locale files)
};

// A character's numbers: an enemy's component, a party member's own. A stat
// not in Base is its default; a resource not in Current is full.
struct Stats
{
    StatValues Base;
    StatValues Current;
};

// ---------------------------------------------------------------------------
// StatSet - the game's stats. The engine's ten (HP, MaxHP, MP, MaxMP, ATK,
// DEF, MAG, RES, SPD, LUK) unless the game's stats file adds to them or
// replaces them:
//
//   { "replace": false,
//     "stats": { "STR":  { "name": "Strength", "default": 10, "max": 99 },
//                "Stamina": { "name": "Stamina", "default": 50, "max_stat": "MaxStamina" },
//                "MaxStamina": { "name": "Max Stamina", "default": 50 } } }
//
// A stat the file names again (HP) takes the file's fields over the engine's.
// English names go to Loc (stats.<id>.name); translations in locale files.
// ---------------------------------------------------------------------------
class StatSet
{
public:
    StatSet(); // the engine's ten

    // Read a game's stats file (a missing one is fine: false, nothing changes).
    bool LoadFile(const std::filesystem::path& file);
    void Load(const nlohmann::json& json);

    [[nodiscard]] const std::vector<StatDef>& All() const { return _stats; }
    [[nodiscard]] const StatDef*              Find(std::string_view id) const;

    // Every stat's value: base (or default), plus what the equipment adds -
    // flat first ("ATK": 5), then percent ("ATK%": 10) - kept within min /
    // max. A resource is its current value, at most its maximum's value.
    [[nodiscard]] StatValues Effective(const Stats& stats, std::span<const ItemDef* const> equipped = {}) const;

    // Change stats by these amounts (an item's "use", a level-up): a
    // resource's current value (within 0 and its maximum), else the base.
    void Apply(Stats& stats, const StatValues& changes, std::span<const ItemDef* const> equipped = {}) const;

    // Give Loc the English names a game's stats file wrote (the engine's own
    // stats have theirs in the engine's locale files).
    void PublishStrings() const;

private:
    [[nodiscard]] f32 Clamp(const StatDef& def, f32 value) const;
    void              Upsert(StatDef def);

    std::vector<StatDef> _stats;
};
} // namespace Arcbit
