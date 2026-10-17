#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/Entity.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Arcbit
{
// One value in an event's payload. A closed set, so every payload can be
// read by Lua (Phase 26), shown by the editor's event monitor (Phase 40) and
// written by a save - which std::any couldn't promise.
using EventValue = std::variant<bool, i64, f64, std::string, Entity, Vec2>;

// ---------------------------------------------------------------------------
// EventPayload - the named fields an event carries.
//
//   events.Publish(CoinCollected, { { "amount", 5 }, { "by", player } });
//   const i64 amount = e.Payload.Get<i64>("amount", 0);
//
// Small and flat on purpose: an event says what happened, it doesn't carry
// game state. Integer literals are stored as i64 and decimals as f64; Get
// converts between the number types, so Get<f32> or Get<i32> of either works.
// A missing field, or one of another kind, returns the fallback.
// ---------------------------------------------------------------------------
class EventPayload
{
public:
    struct Field
    {
        std::string Name;
        EventValue  Value;
    };

    EventPayload() = default;
    EventPayload(std::initializer_list<std::pair<std::string_view, EventValue>> fields);

    // Add a field, or replace the one with this name.
    void Set(std::string_view name, EventValue value);

    // The field's value, or null if there's none.
    [[nodiscard]] const EventValue* Find(std::string_view name) const;
    [[nodiscard]] bool              Has(std::string_view name) const { return Find(name) != nullptr; }

    template<typename T>
    [[nodiscard]] T Get(std::string_view name, T fallback) const;

    [[nodiscard]] const std::vector<Field>& Fields() const { return _fields; }
    [[nodiscard]] bool                      Empty() const { return _fields.empty(); }

    // "amount=5 by=entity(3:1)" - for logs and the event monitor.
    [[nodiscard]] std::string ToString() const;

private:
    std::vector<Field> _fields; // a handful at most: a linear search beats hashing
};

template<typename T>
T EventPayload::Get(const std::string_view name, T fallback) const
{
    const EventValue* value = Find(name);
    if (!value) return fallback;
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        if (const auto* i = std::get_if<i64>(value)) return static_cast<T>(*i);
        if (const auto* f = std::get_if<f64>(value)) return static_cast<T>(*f);
        return fallback;
    }
    else {
        const auto* held = std::get_if<T>(value);
        return held ? *held : fallback;
    }
}
} // namespace Arcbit
