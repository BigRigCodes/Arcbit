#pragma once

#include <arcbit/app/ProjectDefaults.h>
#include <arcbit/items/Stats.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Inventories;
class ItemLibrary;

// ---------------------------------------------------------------------------
// A character who can join the party, from the game's party file
// (assets/party.json, docs/items.md#the-party):
//
//   { "starting": ["hero"],
//     "members": {
//       "hero":  { "name": "Hero", "stats": { "MaxHP": 120, "ATK": 12 },
//                  "equipment": { "weapon": "iron_sword" } },
//       "ayla":  { "name": "Ayla", "stats": { "MAG": 18 }, "bag": "ayla" } } }
//
// "name" is English (Loc key party.<id>.name); "bag" gives the member a bag
// of their own instead of the party's shared one.
// ---------------------------------------------------------------------------
struct CharacterDef
{
    std::string                        Id;
    std::string                        Name;
    std::string                        NameKey;
    StatValues                         Stats;     // over the stats' defaults
    std::map<std::string, std::string> Equipment; // slot -> item, worn when they first join
    std::string                        Bag;       // empty: the party's
};

// A scene entity standing for a party member - the player entity shows the
// leader. Member empty: whoever leads. In a scene file: "PartyLink": {}.
struct PartyLink
{
    std::string Member;
};

// One member: who, their own stats (and resources' current values), what
// they wear, and the bag they use.
struct PartyMember
{
    std::string                        Id;
    Stats                              Own;
    std::map<std::string, std::string> Equipment; // slot -> item
    std::string                        Bag = "party";
};

// ---------------------------------------------------------------------------
// Party - who's in the party (game state, like the bags; saved as the
// "party" section). A new game starts with the party file's "starting"
// members (else one, "player"). The first member is the leader - the one a
// scene's player entity shows (PartyMember component).
//
// Effective stats are worked out when asked: own stats, plus what equipped
// items add (StatSet::Effective).
// ---------------------------------------------------------------------------
class Party
{
public:
    Party(const ItemLibrary& items, const StatSet& stats, Inventories& bags, std::vector<EquipmentSlot> slots);

    // Told as members join / leave, and as items are equipped / unequipped.
    std::function<void(const std::string& member, bool joined)>                                     OnMembership;
    std::function<void(const std::string& member, const std::string& item, const std::string& slot, bool on)> OnEquip;

    // The party file; without one the party is "player" alone.
    bool LoadFile(const std::filesystem::path& file);
    void Load(const nlohmann::json& json);
    void PublishStrings() const;

    [[nodiscard]] const CharacterDef*       FindCharacter(std::string_view id) const;
    [[nodiscard]] const std::vector<PartyMember>& GetMembers() const { return _members; }
    [[nodiscard]] PartyMember*              Find(std::string_view id);
    [[nodiscard]] const PartyMember*        GetLeader() const { return _members.empty() ? nullptr : &_members.front(); }
    [[nodiscard]] const std::vector<EquipmentSlot>& GetSlots() const { return _slots; }

    // A character joins (wearing their starting equipment) / leaves (what
    // they wore goes to their bag). False if already in / not in / unknown.
    bool Join(std::string_view id);
    bool Leave(std::string_view id);
    bool Move(std::string_view id, u32 index); // their place in the order (0: the leader)

    // Effective stats: own, plus equipment.
    [[nodiscard]] StatValues GetStats(std::string_view member) const;

    // Wear an item from the member's bag: in `slot`, else the first free slot
    // its kind fits, else the first such slot (what was there goes back to
    // the bag). False if it isn't equipment, no slot takes it, it isn't in the
    // bag, or what it replaces doesn't fit back.
    bool Equip(std::string_view member, std::string_view item, std::string_view slot = {});
    // Back into their bag; false if the slot is empty or the bag is full.
    bool Unequip(std::string_view member, std::string_view slot);

    // An item's "use" stats on a member (resources within their maximum).
    void ApplyUse(std::string_view member, const ItemDef& item);

    // Back to the starting party (a new game); the save section.
    void                         Reset();
    [[nodiscard]] nlohmann::json ToJson() const;
    void                         FromJson(const nlohmann::json& json);

private:
    [[nodiscard]] std::vector<const ItemDef*> Worn(const PartyMember& member) const;
    [[nodiscard]] const EquipmentSlot*        SlotFor(const PartyMember& member, const ItemDef& item, std::string_view slot) const;
    [[nodiscard]] Inventory*                  BagOf(const PartyMember& member) const;

    const ItemLibrary*                          _items;
    const StatSet*                              _stats;
    Inventories*                                _bags;
    std::vector<EquipmentSlot>                  _slots;
    std::map<std::string, CharacterDef, std::less<>> _characters;
    std::vector<std::string>                    _starting;
    std::vector<PartyMember>                    _members;
};
} // namespace Arcbit
