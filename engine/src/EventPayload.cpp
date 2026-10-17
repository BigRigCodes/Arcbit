#include <arcbit/events/EventPayload.h>

#include <algorithm>
#include <format>

namespace Arcbit
{
namespace
{
    std::string Format(const EventValue& value)
    {
        struct Formatter
        {
            std::string operator()(const bool v) const { return v ? "true" : "false"; }
            std::string operator()(const i64 v) const { return std::to_string(v); }
            std::string operator()(const f64 v) const { return std::format("{}", v); }
            std::string operator()(const std::string& v) const { return std::format("\"{}\"", v); }
            std::string operator()(const Entity& v) const { return std::format("entity({}:{})", v.Index, v.Generation); }
            std::string operator()(const Vec2& v) const { return std::format("({}, {})", v.X, v.Y); }
        };
        return std::visit(Formatter{}, value);
    }
} // namespace

EventPayload::EventPayload(const std::initializer_list<std::pair<std::string_view, EventValue>> fields)
{
    for (const auto& [name, value] : fields) Set(name, value);
}

void EventPayload::Set(const std::string_view name, EventValue value)
{
    const auto it = std::ranges::find(_fields, name, &Field::Name);
    if (it != _fields.end()) it->Value = std::move(value);
    else _fields.push_back({ std::string(name), std::move(value) });
}

const EventValue* EventPayload::Find(const std::string_view name) const
{
    const auto it = std::ranges::find(_fields, name, &Field::Name);
    return it != _fields.end() ? &it->Value : nullptr;
}

std::string EventPayload::ToString() const
{
    std::string text;
    for (const Field& field : _fields) {
        if (!text.empty()) text += ' ';
        text += field.Name + "=" + Format(field.Value);
    }
    return text;
}
} // namespace Arcbit
