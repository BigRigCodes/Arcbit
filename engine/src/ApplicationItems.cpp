// Application: items - reading the item files, the game's bags and
// currencies (game state, saved as the "inventories" section), and the
// console's give / take / items / drop, and items lying in the world
// (docs/items.md).

#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Loc.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/items/Pickup.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
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
    (void)_loot.AddFolder(_config.LootFolder);
    _loot.Check([this](const std::string_view id) { return _items.Has(id); },
                [this](const std::string_view id) { return _currencies->Find(id) != nullptr; });
    (void)_statSet.LoadFile(_config.StatsFile);
    _statSet.PublishStrings();
    (void)_party->LoadFile(_config.PartyFile); // after the items: starting members wear theirs
    _party->PublishStrings();
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
    StartParty();
}

// The party: who's in it and what they wear, its events and save section.
void Application::StartParty()
{
    _party = std::make_unique<Party>(_items, _statSet, *_inventories, _config.Defaults.EquipmentSlots);
    _party->OnMembership = [this](const std::string& member, const bool joined) {
        _events.PublishEngine(joined ? EngineEvents::PartyJoined : EngineEvents::PartyLeft, { { "member", member } });
    };
    _party->OnEquip = [this](const std::string& member, const std::string& item, const std::string& slot, const bool on) {
        _events.PublishEngine(on ? EngineEvents::ItemEquipped : EngineEvents::ItemUnequipped,
                              { { "member", member }, { "item", item }, { "slot", slot } });
    };
    _saves.RegisterParticipant(
        "party", [this](nlohmann::json& section) { section = _party->ToJson(); },
        [this](const nlohmann::json& section) { _party->FromJson(section); });
}

UseResult Application::UseItem(const std::string_view item, const std::string_view member, const std::string_view bagName)
{
    const ItemDef* def = _items.Find(item);
    Inventory*     bag = _inventories->Find(bagName);
    if (!def || !bag || !bag->Has(item)) return UseResult::NotInBag;
    if (def->Use.empty()) return UseResult::NotUsable;
    const std::string who = !member.empty() ? std::string(member) : _party->GetLeader() ? _party->GetLeader()->Id : std::string{};
    if (!_party->Find(who)) return UseResult::NoMember;
    const bool otherActions = std::ranges::any_of(std::array{ "set", "add", "event", "lua" }, [&](const char* k) { return def->Use.contains(k); });
    if (!otherActions && !_party->WouldChange(who, *def)) return UseResult::NoEffect;
    (void)_party->ApplyUse(who, *def);
    RunUseActions(*def, who);
    if (def->Use.value("consume", def->Type == ItemTypes::Consumable)) (void)bag->Remove(item);
    _events.PublishEngine(EngineEvents::ItemUsed, { { "item", def->Id }, { "member", who }, { "bag", std::string(bagName) } });
    return UseResult::Used;
}

// A "use" block's other actions: set / add save data (currencies through
// Currencies), a game event, a Lua statement, a sound.
void Application::RunUseActions(const ItemDef& item, const std::string& member)
{
    const nlohmann::json& use = item.Use;
    for (const auto& [key, value] : use.value("set", nlohmann::json::object()).items())
        if (value.is_boolean()) _saves.Data().Set(key, value.get<bool>());
        else if (value.is_number_integer()) _saves.Data().Set(key, value.get<i64>());
        else if (value.is_number()) _saves.Data().Set(key, value.get<f64>());
        else if (value.is_string()) _saves.Data().Set(key, value.get<std::string>());
    for (const auto& [key, value] : use.value("add", nlohmann::json::object()).items()) {
        if (!value.is_number_integer()) continue;
        if (_currencies->Find(key)) (void)_currencies->Add(key, value.get<i64>());
        else _saves.Data().Set(key, _saves.Data().Get<i64>(key, 0) + value.get<i64>());
    }
    if (const auto e = use.find("event"); e != use.end() && e->is_string())
        _events.Publish(e->get<std::string>(), { { "item", item.Id }, { "member", member } });
    if (const auto lua = use.find("lua"); lua != use.end() && lua->is_string())
        (void)_scriptEngine.Execute(lua->get<std::string>(), std::format("item {}", item.Id));
    if (const auto sound = use.find("sound"); sound != use.end() && sound->is_string())
        AudioManager::PlayOneShot(sound->get<std::string>());
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
        constexpr i64 Most  = std::numeric_limits<u32>::max();
        const i64     count = a.size() > 1 ? DebugConsole::Integer(a[1], "count", 1, Most) : 1;
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
    RegisterPartyCommands();
    RegisterLootCommands();
    _debugConsole.Register({ .Name = "drop", .Help = "drop <item> [count] [bag] - onto the ground, where the leader stands",
                             .Run = [this](const Args& a) {
                                 if (a.empty()) throw std::runtime_error("drop <item> [count] [bag]");
                                 constexpr i64     Most  = std::numeric_limits<u32>::max();
                                 const u32         count = a.size() > 1 ? static_cast<u32>(DebugConsole::Integer(a[1], "count", 1, Most)) : 1;
                                 const std::string from  = a.size() > 2 ? a[2] : std::string(Inventories::Party);
                                 if (!DropItem(a[0], count, from).IsValid()) throw std::runtime_error("nothing dropped - not in the bag, or a key item");
                                 _debugConsole.Print(std::format("dropped {} ({} left)", a[0], _inventories->Find(from)->Count(a[0])));
                             } });
    _debugConsole.Register({ .Name = "items", .Help = "items [bag] - what a bag holds, and the currencies",
                             .Run = [this, bag](const Args& a) {
                                 for (const ItemStack& stack : bag(a, 0).GetStacks())
                                     _debugConsole.Print(std::format("  {} x{}", stack.Item, stack.Count));
                                 for (const CurrencyDef& currency : _currencies->GetAll())
                                     _debugConsole.Print(std::format("  {}: {}", currency.Id, _currencies->Get(currency.Id)));
                             } });
}
} // namespace Arcbit

namespace Arcbit
{
// party lists the members and their stats; equip / unequip / use change them.
void Application::RegisterPartyCommands()
{
    using Args = DebugConsole::Args;
    _debugConsole.Register({ .Name = "party", .Help = "party - who's in the party, their stats and equipment",
                             .Run = [this](const Args&) {
                                 for (const PartyMember& member : _party->GetMembers()) {
                                     std::string line = "  " + member.Id + ":";
                                     for (const auto& [stat, value] : _party->GetStats(member.Id)) line += std::format(" {} {}", stat, value);
                                     for (const auto& [slot, item] : member.Equipment) line += std::format(" [{}: {}]", slot, item);
                                     _debugConsole.Print(line);
                                 }
                             } });
    _debugConsole.Register({ .Name = "equip", .Help = "equip <member> <item> [slot] - from their bag", .Cheat = true,
                             .Run = [this](const Args& a) {
                                 if (a.size() < 2) throw std::runtime_error("equip <member> <item> [slot]");
                                 if (!_party->Equip(a[0], a[1], a.size() > 2 ? a[2] : "")) throw std::runtime_error("can't equip that");
                             } });
    _debugConsole.Register({ .Name = "unequip", .Help = "unequip <member> <slot>", .Cheat = true, .Run = [this](const Args& a) {
                                 if (a.size() < 2) throw std::runtime_error("unequip <member> <slot>");
                                 if (!_party->Unequip(a[0], a[1])) throw std::runtime_error("nothing there, or no room in the bag");
                             } });
    _debugConsole.Register({ .Name = "use", .Help = "use <item> [member] - from the party's bag", .Cheat = true,
                             .Run = [this](const Args& a) {
                                 if (a.empty()) throw std::runtime_error("use <item> [member]");
                                 const UseResult result = UseItem(a[0], a.size() > 1 ? a[1] : "");
                                 if (result != UseResult::Used) throw std::runtime_error(std::format("not used - {}", UseResultName(result)));
                                 _debugConsole.Print(std::format("used {} ({} left)", a[0], _inventories->GetParty().Count(a[0])));
                             } });
}
} // namespace Arcbit

namespace Arcbit
{
// The scenes' pickups go into the game's bags; the stock pickup prefab, for
// drops, unless the game has its own.
void Application::StartPickups()
{
    _itemHost.Take  = [this](const std::string_view item, const u32 count, const std::string_view bag) { return TakePickup(item, count, bag); };
    _itemHost.Dress = [this](Scene& scene, const Entity pickup) { DressPickup(scene, pickup); };
    _itemHost.Open  = [this](Scene&, const Entity container, const Entity by) { OpenContainer(container, by); };
    _itemHost.DropLoot = [this](Scene& scene, const std::string_view table, const Vec2 at) { DropLoot(scene, table, at); };
    if (!_prefabs.Has(PickupPrefab))
        _prefabs.Add(std::string(PickupPrefab), { { "Tag", { { "value", "Pickup" } } }, { "ItemPickup", nlohmann::json::object() },
                                                  { "Persistent", nlohmann::json::object() } });
}

// A currency picked up counts however it's kept; an item goes into the bag.
u32 Application::TakePickup(const std::string_view item, const u32 count, const std::string_view bag)
{
    if (_currencies->Find(item)) return static_cast<u32>(_currencies->Add(item, count)); // Add returns the change
    Inventory* into = _inventories->Find(bag);
    return into ? into->Add(item, count) : 0;
}

// A pickup with no look of its own: the item's icon (a texture, or
// "sheet.json#frame"), else a white square - half a tile across.
void Application::DressPickup(Scene& scene, const Entity pickup)
{
    World&            world = scene.GetWorld();
    const ItemPickup* p     = world.GetComponent<ItemPickup>(pickup);
    const ItemDef*    def   = p ? _items.Find(p->Item) : nullptr;
    SpriteRenderer    look{ .Texture = GetWhiteTexture(), .Sampler = GetStockSampler(StockSampler::Nearest) };
    const std::string icon = def ? def->Icon : std::string{};
    if (const size_t hash = icon.find('#'); hash != std::string::npos) {
        const SpriteSheet* sheet = scene.LoadSpriteSheet(icon.substr(0, hash));
        const auto         frame = sheet ? sheet->GetFrame(icon.substr(hash + 1)) : std::nullopt;
        if (frame) {
            look.Texture = sheet->GetTexture();
            look.UV      = frame->UV;
        }
    } else if (!icon.empty()) {
        if (const TextureHandle texture = scene.LoadTexture(icon); texture.IsValid()) look.Texture = texture;
    }
    world.AddComponent<SpriteRenderer>(pickup, look);
    const f32 half = scene.GetConfig().TileSize * 0.5f;
    if (Transform2D* t = world.GetTransform(pickup)) t->Scale = { half, half };
}

namespace
{
    // Where a drop lands: the leader's entity, else any party entity, else
    // the middle of the view.
    Vec2 DropPoint(Scene& scene, const std::string& leader)
    {
        std::optional<Vec2> any, theirs;
        scene.GetWorld().Query<PartyLink, Transform2D>().ForEach([&](const PartyLink& link, const Transform2D& t) {
            if (!any) any = t.Position;
            if (!theirs && (link.Member.empty() || link.Member == leader)) theirs = t.Position;
        });
        return theirs.value_or(any.value_or(scene.GetCamera().Position));
    }
} // namespace

// A pickup of `count` x `item` at `at`: the item's drop_prefab, else the
// stock pickup (a currency, or anything without a definition, always that).
Entity Application::SpawnPickup(Scene& scene, const ItemDef* def, const std::string_view item, const u32 count, const Vec2 at)
{
    const std::string prefab = def && !def->DropPrefab.empty() ? def->DropPrefab : std::string(PickupPrefab);
    if (!_prefabs.Has(prefab)) return Entity::Invalid();
    const Entity e = scene.Spawn(prefab, at);
    if (!e.IsValid()) return e;
    World& world = scene.GetWorld();
    if (!world.GetComponent<ItemPickup>(e)) world.AddComponent<ItemPickup>(e);
    ItemPickup& pickup = *world.GetComponent<ItemPickup>(e);
    pickup.Item        = std::string(item);
    pickup.Count       = count;
    return e;
}

Entity Application::DropItem(const std::string_view item, const u32 count, const std::string_view bagName)
{
    const ItemDef* def   = _items.Find(item);
    Inventory*     bag   = _inventories->Find(bagName);
    Scene*         scene = _scenes.GetActive();
    if (!def || !bag || !scene || count == 0 || def->Type == ItemTypes::Key) return Entity::Invalid();
    const std::string prefab = def->DropPrefab.empty() ? std::string(PickupPrefab) : def->DropPrefab;
    if (!_prefabs.Has(prefab) || !bag->Has(item)) return Entity::Invalid();

    const u32    dropped = bag->Remove(item, count);
    const Entity e       = SpawnPickup(*scene, def, item, dropped, DropPoint(*scene, _party->GetLeader() ? _party->GetLeader()->Id : ""));
    if (!e.IsValid()) {
        (void)bag->Add(item, dropped); // back where it was
        return Entity::Invalid();
    }
    ItemPickup& pickup = *scene->GetWorld().GetComponent<ItemPickup>(e);
    pickup.Bag         = std::string(bagName);
    pickup._armed      = false; // not straight back up from under the player's feet
    _events.PublishEngine(EngineEvents::ItemDropped,
                          { { "item", def->Id }, { "count", dropped }, { "bag", std::string(bagName) }, { "entity", e } });
    return e;
}

// A party entity whose member's bag is over its weight limit walks at the
// project's EncumberedSpeed - possible only when the bag allows going over.
void Application::UpdateEncumbrance(Scene& scene)
{
    bool heavy = false;
    scene.GetWorld().Query<PartyLink, MoveIntent>().ForEach([&](const PartyLink& link, MoveIntent& intent) {
        const PartyMember* member = link.Member.empty() ? _party->GetLeader() : _party->Find(link.Member);
        const Inventory*   bag    = member ? _inventories->Find(member->Bag) : nullptr;
        const bool         over   = bag && bag->IsOverWeight();
        intent.SpeedScale         = over ? _config.Defaults.EncumberedSpeed : 1.0f;
        heavy                     = heavy || over;
    });
    if (heavy == _encumbered) return;
    _encumbered = heavy;
    _events.PublishEngine(EngineEvents::PartyEncumbered, { { "encumbered", heavy } });
}
} // namespace Arcbit
