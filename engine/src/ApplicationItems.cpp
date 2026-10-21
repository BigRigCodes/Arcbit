// Application: items - reading the item files, the game's bags and
// currencies (game state, saved as the "inventories" section), and the
// console's give / take / items (docs/items.md).

#include <arcbit/app/Application.h>
#include <arcbit/core/Loc.h>
#include <arcbit/events/EngineEvents.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>

namespace Arcbit
{
// The items, checked against the project's stack sizes and equipment slots.
void Application::LoadItems()
{
    ItemLibrary::Rules& rules = _items.ItemRules;
    rules.StackByType         = _config.Defaults.ItemStack;
    rules.Stack               = _config.Defaults.ItemStackOther;
    rules.AssetRoot           = std::filesystem::current_path();
    for (const EquipmentSlot& slot : _config.Defaults.EquipmentSlots)
        if (std::ranges::find(rules.SlotKinds, slot.Kind) == rules.SlotKinds.end()) rules.SlotKinds.push_back(slot.Kind);
    (void)_items.AddFolder(_config.ItemFolder);
}

// The party's bag and the currencies, their events, and their save section.
void Application::StartInventories()
{
    _inventories = std::make_unique<Inventories>(_items);
    _inventories->AddStarting(std::string(Inventories::Party), _config.Defaults.PartyBag);
    _inventories->OnChange = [this](const std::string& bag, const std::string& item, const i64 delta) {
        _events.PublishEngine(delta > 0 ? EngineEvents::ItemAdded : EngineEvents::ItemRemoved,
                              { { "bag", bag }, { "item", item }, { "count", delta > 0 ? delta : -delta } });
    };
    _currencies = std::make_unique<Currencies>(_saves.Data(), *_inventories);
    _currencies->SetCurrencies(_config.Defaults.Currencies);
    _currencies->OnChange = [this](const std::string& id, const i64 amount, const i64 delta) {
        _events.PublishEngine(EngineEvents::CurrencyChanged, { { "currency", id }, { "amount", amount }, { "change", delta } });
    };
    _saves.RegisterParticipant(
        "inventories", [this](nlohmann::json& section) { section = _inventories->ToJson(); },
        [this](const nlohmann::json& section) { _inventories->FromJson(section); });
}

// give / take an item or a currency; items lists a bag and the currencies.
void Application::RegisterItemCommands()
{
    using Args       = DebugConsole::Args;
    const auto bag   = [this](const Args& a, const size_t at) -> Inventory& {
        const std::string name = a.size() > at ? a[at] : std::string(Inventories::Party);
        Inventory*        found = _inventories->Find(name);
        if (!found) throw std::runtime_error(std::format("no bag '{}'", name));
        return *found;
    };
    const auto change = [this, bag](const Args& a, const i64 sign) {
        if (a.empty()) throw std::runtime_error("which item?");
        const i64 count = a.size() > 1 ? std::stoll(a[1]) : 1;
        if (_currencies->Find(a[0])) return _debugConsole.Print(std::format("{} {}", a[0], _currencies->Add(a[0], sign * count)));
        if (!_items.Has(a[0])) throw std::runtime_error(std::format("no item '{}'", a[0]));
        Inventory& into  = bag(a, 2);
        const u32  moved = sign > 0 ? into.Add(a[0], static_cast<u32>(count)) : into.Remove(a[0], static_cast<u32>(count));
        _debugConsole.Print(std::format("{} {} (now {})", sign > 0 ? "gave" : "took", moved, into.Count(a[0])));
    };
    _debugConsole.Register({ .Name = "give", .Help = "give <item | currency> [count] [bag] - into the party's bag", .Cheat = true,
                             .Run = [change](const Args& a) { change(a, 1); } });
    _debugConsole.Register({ .Name = "take", .Help = "take <item | currency> [count] [bag]", .Cheat = true,
                             .Run = [change](const Args& a) { change(a, -1); } });
    _debugConsole.Register({ .Name = "items", .Help = "items [bag] - what a bag holds, and the currencies",
                             .Run = [this, bag](const Args& a) {
                                 for (const ItemStack& stack : bag(a, 0).GetStacks())
                                     _debugConsole.Print(std::format("  {} x{}", stack.Item, stack.Count));
                                 for (const CurrencyDef& currency : _currencies->GetAll())
                                     _debugConsole.Print(std::format("  {}: {}", currency.Id, _currencies->Get(currency.Id)));
                             } });
}
} // namespace Arcbit
