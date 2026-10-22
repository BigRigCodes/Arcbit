#include <arcbit/items/Party.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace Arcbit
{
namespace
{
using nlohmann::json;
constexpr std::string_view DefaultMember = "player";
} // namespace

Party::Party(const ItemLibrary& items, const StatSet& stats, Inventories& bags, std::vector<EquipmentSlot> slots)
    : _items(&items), _stats(&stats), _bags(&bags), _slots(std::move(slots))
{
    _characters[std::string(DefaultMember)] = { .Id = std::string(DefaultMember), .Name = "Player", .NameKey = "party.player.name" };
    _starting = { std::string(DefaultMember) };
    Reset();
}

bool Party::LoadFile(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) return false;
    try {
        Load(json::parse(in, nullptr, true, true));
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "Party: '{}' - {}", file.string(), e.what());
        return false;
    }
}

void Party::Load(const json& j)
{
    for (const auto& [id, c] : j.value("members", json::object()).items()) {
        CharacterDef def{ .Id = id, .Name = c.value("name", id), .NameKey = std::format("party.{}.name", id), .Bag = c.value("bag", std::string{}) };
        for (const auto& [stat, value] : c.value("stats", json::object()).items()) def.Stats[stat] = value.get<f32>();
        def.Equipment = c.value("equipment", std::map<std::string, std::string>{});
        _characters[id] = std::move(def);
    }
    if (j.contains("starting")) _starting = j["starting"].get<std::vector<std::string>>();
    Reset();
}

void Party::PublishStrings() const
{
    std::vector<std::pair<std::string, std::string>> strings;
    for (const auto& [id, def] : _characters)
        if (id != DefaultMember) strings.emplace_back(def.NameKey, def.Name); // "player" is in the engine's locale files
    Loc::SetStrings(Loc::English, "party", strings);
}

const CharacterDef* Party::FindCharacter(const std::string_view id) const
{
    const auto it = _characters.find(id);
    return it != _characters.end() ? &it->second : nullptr;
}

PartyMember* Party::Find(const std::string_view id)
{
    const auto it = std::ranges::find(_members, id, &PartyMember::Id);
    return it != _members.end() ? &*it : nullptr;
}

// --- Joining and leaving ------------------------------------------------------

bool Party::Join(const std::string_view id)
{
    const CharacterDef* def = FindCharacter(id);
    if (!def || Find(id)) {
        LOG_WARN(Engine, "Party: '{}' can't join - {}", id, def ? "already in" : "no such character");
        return false;
    }
    PartyMember member{ .Id = def->Id, .Own = { .Base = def->Stats }, .Bag = def->Bag.empty() ? "party" : def->Bag };
    if (!_bags->Find(member.Bag)) _bags->Create(member.Bag, _bags->GetParty().GetRules());
    for (const auto& [slot, item] : def->Equipment) {
        const ItemDef* worn = _items->Find(item);
        if (worn && SlotFor(member, *worn, slot)) member.Equipment[slot] = item;
        else LOG_WARN(Engine, "Party: '{}' can't wear '{}' in '{}'", id, item, slot);
    }
    _members.push_back(std::move(member));
    if (OnMembership) OnMembership(def->Id, true);
    return true;
}

bool Party::Leave(const std::string_view id)
{
    const auto it = std::ranges::find(_members, id, &PartyMember::Id);
    if (it == _members.end()) return false;
    PartyMember gone = std::move(*it);
    _members.erase(it);
    if (Inventory* bag = _bags->Find(gone.Bag))
        for (const auto& [slot, item] : gone.Equipment)
            if (bag->Add(item) == 0) LOG_WARN(Engine, "Party: no room for '{}' as '{}' left - lost", item, gone.Id);
    if (OnMembership) OnMembership(gone.Id, false);
    return true;
}

bool Party::Move(const std::string_view id, const u32 index)
{
    const auto it = std::ranges::find(_members, id, &PartyMember::Id);
    if (it == _members.end() || index >= _members.size()) return false;
    PartyMember member = std::move(*it);
    _members.erase(it);
    _members.insert(_members.begin() + index, std::move(member));
    return true;
}

// --- Stats and equipment ---------------------------------------------------------

std::vector<const ItemDef*> Party::Worn(const PartyMember& member) const
{
    std::vector<const ItemDef*> worn;
    for (const auto& [slot, item] : member.Equipment)
        if (const ItemDef* def = _items->Find(item)) worn.push_back(def);
    return worn;
}

StatValues Party::GetStats(const std::string_view id) const
{
    const auto it = std::ranges::find(_members, id, &PartyMember::Id);
    return it != _members.end() ? _stats->Effective(it->Own, Worn(*it)) : StatValues{};
}

Inventory* Party::BagOf(const PartyMember& member) const { return _bags->Find(member.Bag); }

// The slot an item goes in: the one named (if it takes the item), else the
// first free one of its kind, else the first of its kind.
const EquipmentSlot* Party::SlotFor(const PartyMember& member, const ItemDef& item, const std::string_view slot) const
{
    if (item.Type != ItemTypes::Equipment || item.Slot.empty()) return nullptr;
    const EquipmentSlot* first = nullptr;
    for (const EquipmentSlot& s : _slots) {
        if (s.Kind != item.Slot) continue;
        if (!slot.empty()) {
            if (s.Name == slot) return &s;
            continue;
        }
        if (!member.Equipment.contains(s.Name)) return &s;
        if (!first) first = &s;
    }
    return slot.empty() ? first : nullptr;
}

bool Party::Equip(const std::string_view id, const std::string_view item, const std::string_view slotName)
{
    PartyMember*         member = Find(id);
    const ItemDef*       def    = _items->Find(item);
    const EquipmentSlot* slot   = member && def ? SlotFor(*member, *def, slotName) : nullptr;
    Inventory*           bag    = member ? BagOf(*member) : nullptr;
    if (!slot || !bag || !bag->Has(item)) return false;
    (void)bag->Remove(item);
    if (const auto was = member->Equipment.find(slot->Name); was != member->Equipment.end()) {
        if (bag->Add(was->second) == 0) { // no room for what it replaces: as it was
            (void)bag->Add(item);
            return false;
        }
        if (OnEquip) OnEquip(member->Id, was->second, slot->Name, false);
    }
    member->Equipment[slot->Name] = std::string(item);
    if (OnEquip) OnEquip(member->Id, std::string(item), slot->Name, true);
    return true;
}

bool Party::Unequip(const std::string_view id, const std::string_view slotName)
{
    PartyMember* member = Find(id);
    Inventory*   bag    = member ? BagOf(*member) : nullptr;
    if (!bag) return false;
    const auto worn = member->Equipment.find(std::string(slotName));
    if (worn == member->Equipment.end() || bag->Add(worn->second) == 0) return false;
    const std::string item = worn->second;
    member->Equipment.erase(worn);
    if (OnEquip) OnEquip(member->Id, item, std::string(slotName), false);
    return true;
}

void Party::ApplyUse(const std::string_view id, const ItemDef& item)
{
    PartyMember* member = Find(id);
    if (!member) return;
    StatValues changes;
    for (const auto& [stat, amount] : item.Use.value("stats", json::object()).items())
        if (amount.is_number()) changes[stat] = amount.get<f32>();
    _stats->Apply(member->Own, changes, Worn(*member));
}

// --- Saving ------------------------------------------------------------------------

void Party::Reset()
{
    _members.clear();
    for (const std::string& id : _starting) (void)Join(id);
}

json Party::ToJson() const
{
    json members = json::array();
    for (const PartyMember& m : _members)
        members.push_back({ { "id", m.Id }, { "base", m.Own.Base }, { "current", m.Own.Current }, { "equipment", m.Equipment }, { "bag", m.Bag } });
    return { { "members", std::move(members) } };
}

// An empty section is a new game; a member the party file no longer has
// stays (their saved stats are all they need).
void Party::FromJson(const json& j)
{
    if (!j.contains("members")) return Reset();
    _members.clear();
    for (const json& m : j["members"]) {
        PartyMember member{ .Id = m.value("id", std::string{}), .Bag = m.value("bag", std::string("party")) };
        member.Own.Base    = m.value("base", StatValues{});
        member.Own.Current = m.value("current", StatValues{});
        member.Equipment   = m.value("equipment", std::map<std::string, std::string>{});
        if (!member.Id.empty()) _members.push_back(std::move(member));
    }
}
} // namespace Arcbit
