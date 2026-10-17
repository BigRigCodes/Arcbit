#include <arcbit/save/SaveData.h>

#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <ranges>

namespace Arcbit
{
void SaveData::Set(const std::string_view key, SaveValue value)
{
    if (const auto it = _values.find(key); it != _values.end()) it->second = std::move(value);
    else _values.emplace(std::string(key), std::move(value));
}

void SaveData::Erase(const std::string_view key)
{
    if (const auto it = _values.find(key); it != _values.end()) _values.erase(it);
}

bool SaveData::Has(const std::string_view key) const { return _values.contains(key); }

const SaveValue* SaveData::Find(const std::string_view key) const
{
    const auto it = _values.find(key);
    return it != _values.end() ? &it->second : nullptr;
}

std::vector<std::string> SaveData::Keys() const
{
    std::vector<std::string> keys;
    keys.reserve(_values.size());
    for (const auto& key : _values | std::views::keys) keys.push_back(key);
    return keys;
}

nlohmann::json SaveData::ToJson() const
{
    nlohmann::json json = nlohmann::json::object();
    for (const auto& [key, value] : _values) {
        std::visit(
            [&]<typename V>(const V& v) {
                if constexpr (std::is_same_v<V, Vec2>) json[key] = nlohmann::json::array({ v.X, v.Y });
                else json[key] = v;
            },
            value);
    }
    return json;
}

void SaveData::FromJson(const nlohmann::json& json)
{
    _values.clear();
    if (!json.is_object()) return;
    for (const auto& [key, value] : json.items()) {
        if (value.is_boolean()) Set(key, value.get<bool>());
        else if (value.is_number_integer()) Set(key, value.get<i64>());
        else if (value.is_number_float()) Set(key, value.get<f64>());
        else if (value.is_string()) Set(key, value.get<std::string>());
        else if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number())
            Set(key, Vec2{ value[0].get<f32>(), value[1].get<f32>() });
        else LOG_WARN(Engine, "SaveData: '{}' isn't a saveable value - skipped", key);
    }
}
} // namespace Arcbit
