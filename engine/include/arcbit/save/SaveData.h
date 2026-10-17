#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/NumberCast.h>
#include <arcbit/core/Types.h>

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace Arcbit
{
// One saved value. The event payload's types without Entity - an entity
// handle means nothing after a reload - so Lua (Phase 26) reads both the same.
using SaveValue = std::variant<bool, i64, f64, std::string, Vec2>;

// ---------------------------------------------------------------------------
// SaveData - the game's own flags and counters, saved with the game.
//
//   saves.Data().Set("quest.dragon", "started");
//   saves.Data().Set("gold", 120);
//   const i64 gold = saves.Data().Get<i64>("gold", 0);
//
// Keys are strings - dot paths by convention, like event names. Integer
// literals are stored as i64 and decimals as f64; Get converts between the
// number types and returns the fallback for a missing key, another kind of
// value, or a number that doesn't fit (as EventPayload::Get does). Systems
// with structured state (inventory, quests) save through participants
// (SaveManager::RegisterParticipant) instead.
// ---------------------------------------------------------------------------
class SaveData
{
public:
    void Set(std::string_view key, SaveValue value);
    void Erase(std::string_view key);
    void Clear() { _values.clear(); }

    [[nodiscard]] bool             Has(std::string_view key) const;
    [[nodiscard]] const SaveValue* Find(std::string_view key) const;
    [[nodiscard]] size_t           Size() const { return _values.size(); }

    template<typename T>
    [[nodiscard]] T Get(std::string_view key, T fallback) const;

    // Every key, sorted - for debugging and the editor.
    [[nodiscard]] std::vector<std::string> Keys() const;

    // JSON object of the values: bools, numbers and strings as themselves,
    // a Vec2 as [x, y]. FromJson skips anything else (logged).
    [[nodiscard]] nlohmann::json ToJson() const;
    void                         FromJson(const nlohmann::json& json);

    bool operator==(const SaveData& other) const = default;

private:
    std::map<std::string, SaveValue, std::less<>> _values; // sorted: stable save files
};

template<typename T>
T SaveData::Get(const std::string_view key, T fallback) const
{
    const SaveValue* value = Find(key);
    if (!value) return fallback;
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        if (const auto* i = std::get_if<i64>(value)) return ConvertNumber<T>(*i, fallback);
        if (const auto* f = std::get_if<f64>(value)) return ConvertNumber<T>(*f, fallback);
        return fallback;
    }
    else {
        const auto* held = std::get_if<T>(value);
        return held ? *held : fallback;
    }
}
} // namespace Arcbit
