#include <arcbit/items/Inventories.h>

#include <ranges>

namespace Arcbit
{
void Inventories::AddStarting(std::string name, InventoryRules rules)
{
    Create(name, rules);
    _starting.emplace_back(std::move(name), std::move(rules));
}

Inventory& Inventories::Create(std::string name, InventoryRules rules)
{
    Inventory& bag = _bags.insert_or_assign(name, Inventory(*_items, std::move(rules))).first->second;
    Watch(name, bag);
    return bag;
}

void Inventories::Watch(const std::string& name, Inventory& bag)
{
    bag.OnChange = [this, name](const std::string& item, const i64 delta) {
        if (OnChange) OnChange(name, item, delta);
    };
}

Inventory* Inventories::Find(const std::string_view name)
{
    const auto it = _bags.find(name);
    return it != _bags.end() ? &it->second : nullptr;
}

const Inventory* Inventories::Find(const std::string_view name) const
{
    const auto it = _bags.find(name);
    return it != _bags.end() ? &it->second : nullptr;
}

std::vector<std::string> Inventories::GetNames() const
{
    std::vector<std::string> names;
    for (const std::string& name : _bags | std::views::keys) names.push_back(name);
    return names;
}

void Inventories::Reset()
{
    _bags.clear();
    for (const auto& [name, rules] : _starting) Create(name, rules);
}

nlohmann::json Inventories::ToJson() const
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, bag] : _bags) out[name] = bag.ToJson();
    return out;
}

// Loading isn't a change the player made: no OnChange while it happens.
void Inventories::FromJson(const nlohmann::json& json)
{
    Reset();
    for (const auto& [name, saved] : json.items()) {
        Inventory bag(*_items, {});
        bag.FromJson(saved);
        Watch(name, _bags.insert_or_assign(name, std::move(bag)).first->second);
    }
}
} // namespace Arcbit
