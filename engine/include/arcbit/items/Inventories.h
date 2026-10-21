#pragma once

#include <arcbit/items/Inventory.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Inventories - the game's own bags, by name (docs/items.md): game state like
// SaveData, so they don't belong to any scene. "party" - the party's shared
// bag - exists from the start; a game adds others ("stash", a member's own).
// Application saves them as the "inventories" section; a new game starts
// them over, empty, with the rules they began with.
//
// Bags inside a scene (a chest's) are Container components instead.
// ---------------------------------------------------------------------------
class Inventories
{
public:
    static constexpr std::string_view Party = "party";

    explicit Inventories(const ItemLibrary& items) : _items(&items) {}
    Inventories(const Inventories&)            = delete;
    Inventories& operator=(const Inventories&) = delete;

    // Told whenever any bag's count of an item changes: the bag, the item,
    // and the change (+ / -).
    std::function<void(const std::string& bag, const std::string& item, i64 delta)> OnChange;

    // A bag a new game starts with (empty). The project's party bag is one.
    void AddStarting(std::string name, InventoryRules rules);

    // A bag now (empty), replacing one of that name.
    Inventory& Create(std::string name, InventoryRules rules);

    [[nodiscard]] Inventory*               Find(std::string_view name);
    [[nodiscard]] const Inventory*         Find(std::string_view name) const;
    [[nodiscard]] Inventory&               GetParty() { return *Find(Party); }
    [[nodiscard]] std::vector<std::string> GetNames() const;

    // Every bag back to how a new game starts.
    void Reset();

    // The save section: every bag, its rules and its stacks. Reading an empty
    // one is a new game (Reset); a starting bag the save lacks starts empty.
    [[nodiscard]] nlohmann::json ToJson() const;
    void                         FromJson(const nlohmann::json& json);

private:
    void Watch(const std::string& name, Inventory& bag);

    const ItemLibrary*                               _items;
    std::vector<std::pair<std::string, InventoryRules>> _starting;
    std::map<std::string, Inventory, std::less<>>    _bags;
};
} // namespace Arcbit
