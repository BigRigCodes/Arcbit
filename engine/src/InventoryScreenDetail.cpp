// InventoryScreen: the selected item's details and actions, the equipment
// panel, and the footer - carry weight, currencies, the last message.

#include <arcbit/ui/InventoryScreen.h>

#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/items/Stats.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <format>

namespace Arcbit
{
namespace
{
    std::string Number(const f32 value) { return std::format("{:g}", std::round(value * 100.0f) / 100.0f); }

    // "ATK" -> "Attack", "SPD%" -> "Speed" (percent: true).
    std::string StatName(const StatSet* stats, std::string_view id, bool* percent = nullptr)
    {
        const bool isPercent = id.ends_with('%');
        if (percent) *percent = isPercent;
        if (isPercent) id.remove_suffix(1);
        const StatDef* def = stats ? stats->Find(id) : nullptr;
        return def ? Loc::Get(def->NameKey) : std::string(id);
    }

    void SetText(UIWidget* widget, std::string text)
    {
        if (auto* label = dynamic_cast<Label*>(widget)) label->Text = std::move(text);
    }
} // namespace

// ---------------------------------------------------------------------------
// The selected item
// ---------------------------------------------------------------------------

void InventoryScreen::ShowDetail()
{
    const ItemDef* def = SelectedDef();
    SetIcon(FindWidget<Image>("detail-icon"), def ? def->Icon : std::string_view{});
    SetText(FindWidget<Label>("detail-name"), def ? Loc::Get(def->NameKey) : std::string{});
    SetText(FindWidget<Label>("detail-desc"), def ? Loc::Get(def->DescriptionKey) : std::string{});
    SetText(FindWidget<Label>("detail-info"),
            def ? Loc::Format("ui.inventory.info", { { "weight", Number(def->Weight) }, { "value", def->Value } }) : std::string{});
    SetText(FindWidget<Label>("detail-stats"), def ? DescribeStats(*def) : std::string{});
    ShowActions(def);
}

void InventoryScreen::ShowActions(const ItemDef* def)
{
    if (auto* use = FindWidget<Button>("use")) use->Visible = def && !def->Use.empty() && Host.Use;
    if (auto* equip = FindWidget<Button>("equip")) equip->Visible = def && !def->Slot.empty() && Host.Members;
    if (auto* drop = FindWidget<Button>("drop")) drop->Visible = def && Host.Drop && def->Type != ItemTypes::Key;
}

// What it does: the stats it gives worn ("Attack +5", "Speed -5%"), and
// what wearing it would do to the member's; what using it does ("HP +50").
std::string InventoryScreen::DescribeStats(const ItemDef& def) const
{
    std::string text;
    const auto  line = [&text](const std::string& add) { text += (text.empty() ? "" : "\n") + add; };
    for (const auto& [stat, amount] : def.Stats) {
        bool              percent = false;
        const std::string name    = StatName(Host.Stats, stat, &percent);
        line(std::format("{} {}{}{}", name, amount >= 0.0f ? "+" : "", Number(amount), percent ? "%" : ""));
    }
    for (const auto& [stat, amount] : def.Use.value("stats", nlohmann::json::object()).items())
        if (amount.is_number())
            line(Loc::Format("ui.inventory.on_use", { { "stat", StatName(Host.Stats, stat) },
                                                      { "amount", std::format("{}{}", amount.get<f32>() >= 0 ? "+" : "", Number(amount.get<f32>())) } }));
    if (!def.Slot.empty()) {
        const std::string compare = CompareEquip(def);
        if (!compare.empty()) line(compare);
    }
    return text;
}

// The member's stats now and with this worn - in the slot it would go (the
// first free one of its kind, else the first of its kind): only those that
// change, better in green, worse in red.
std::string InventoryScreen::CompareEquip(const ItemDef& def) const
{
    const PartyMember* member = Host.Members ? Host.Members->Find(_member) : nullptr;
    if (!member || !Host.Stats || !Host.Items) return {};
    std::string slot;
    for (const EquipmentSlot& s : Host.Members->GetSlots())
        if (s.Kind == def.Slot && (slot.empty() || !member->Equipment.contains(s.Name))) {
            const bool firstFree = !member->Equipment.contains(s.Name);
            if (slot.empty() || firstFree) slot = s.Name;
            if (firstFree) break;
        }
    if (slot.empty()) return {};
    std::vector<const ItemDef*> now, then;
    for (const auto& [at, item] : member->Equipment)
        if (const ItemDef* worn = Host.Items->Find(item)) {
            now.push_back(worn);
            if (at != slot) then.push_back(worn);
        }
    then.push_back(&def);
    const StatValues before = Host.Stats->Effective(member->Own, now), after = Host.Stats->Effective(member->Own, then);
    std::string      text;
    for (const auto& [stat, value] : after) {
        const f32 was = before.contains(stat) ? before.at(stat) : 0.0f;
        if (std::abs(value - was) < 0.001f) continue;
        text += (text.empty() ? "" : "\n") +
                Loc::Format("ui.inventory.compare", { { "stat", StatName(Host.Stats, stat) }, { "from", Number(was) },
                                                      { "to", std::format("<color={}>{}</color>", value > was ? "green" : "red", Number(value)) } });
    }
    return text;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void InventoryScreen::UseSelected()
{
    const ItemDef* def = SelectedDef();
    if (!def || !Host.Use) return;
    const std::string name   = Loc::Get(def->NameKey);
    const UseResult   result = Host.Use(def->Id, _member, Bag);
    if (result == UseResult::Used) SetMessage("ui.inventory.used", { { "item", name } });
    else SetMessage(std::format("ui.inventory.not_used.{}", UseResultName(result)), { { "item", name } });
    Refresh();
}

void InventoryScreen::EquipSelected()
{
    const ItemDef* def = SelectedDef();
    if (!def || !Host.Members) return;
    const bool worn = Host.Members->Equip(_member, def->Id);
    SetMessage(worn ? "ui.inventory.equipped" : "ui.inventory.cant_equip", { { "item", Loc::Get(def->NameKey) } });
    Refresh();
}

void InventoryScreen::DropSelected()
{
    const ItemDef* def = SelectedDef();
    if (!def || !Host.Drop) return;
    const bool dropped = def->Type != ItemTypes::Key && Host.Drop(def->Id, 1, Bag);
    SetMessage(dropped ? "ui.inventory.dropped" : "ui.inventory.cant_drop", { { "item", Loc::Get(def->NameKey) } });
    Refresh();
}

void InventoryScreen::Unequip(const std::string_view slot)
{
    const PartyMember* member = Host.Members ? Host.Members->Find(_member) : nullptr;
    const auto         worn   = member ? member->Equipment.find(std::string(slot)) : decltype(member->Equipment.end()){};
    if (!member || worn == member->Equipment.end()) return;
    const ItemDef*    def  = Host.Items ? Host.Items->Find(worn->second) : nullptr;
    const std::string name = def ? Loc::Get(def->NameKey) : worn->second;
    const bool        off  = Host.Members->Unequip(_member, slot);
    SetMessage(off ? "ui.inventory.unequipped" : "ui.inventory.cant_unequip", { { "item", name } });
    Refresh();
}

void InventoryScreen::SelectMember(const i32 step)
{
    if (!Host.Members || Host.Members->GetMembers().empty()) return;
    const auto& members = Host.Members->GetMembers();
    const auto  at      = std::ranges::find_if(members, [&](const PartyMember& m) { return m.Id == _member; });
    const i32   n       = static_cast<i32>(members.size());
    const i32   index   = at == members.end() ? 0 : static_cast<i32>(at - members.begin());
    _member             = members[static_cast<size_t>(((index + step) % n + n) % n)].Id;
    Refresh();
}

// Kept as a key and its values, so the language can change under it.
void InventoryScreen::SetMessage(const std::string_view key, const std::vector<std::pair<std::string, std::string>>& args)
{
    _messageKey  = std::string(key);
    _messageArgs = args;
}

// ---------------------------------------------------------------------------
// The equipment panel and the footer
// ---------------------------------------------------------------------------

void InventoryScreen::ShowEquipment()
{
    auto*              panel  = FindWidget<UIWidget>("equipment");
    const PartyMember* member = Host.Members ? Host.Members->Find(_member) : nullptr;
    if (panel) panel->Visible = member != nullptr;
    if (!member) return;
    const CharacterDef* character = Host.Members->FindCharacter(member->Id);
    SetText(FindWidget<Label>("member-name"), character ? Loc::Get(character->NameKey) : InventoryScreen::NameOr("party." + member->Id + ".name", member->Id));
    for (const char* arrow : { "member-prev", "member-next" })
        if (auto* button = FindWidget<Button>(arrow)) button->Visible = Host.Members->GetMembers().size() > 1;
    SetText(FindWidget<Label>("member-stats"), MemberStats());

    auto*       slots = FindWidget<UIWidget>("slots");
    const auto& defs  = Host.Members->GetSlots();
    if (slots && _slotRows.size() != defs.size()) {
        slots->ClearChildren();
        _slotRows.clear();
        for (UIWidget* made : AddFromTemplate("slot-row", *slots, static_cast<u32>(defs.size())))
            if (auto* row = dynamic_cast<NineSliceButton*>(made)) _slotRows.push_back(row);
    }
    const f32 height = GetMetaF32("slot_row_height", 32.0f);
    for (u32 i = 0; i < _slotRows.size() && i < defs.size(); ++i) {
        NineSliceButton&     row  = *_slotRows[i];
        const EquipmentSlot& slot = defs[i];
        const auto           worn = member->Equipment.find(slot.Name);
        const ItemDef*       def  = worn != member->Equipment.end() && Host.Items ? Host.Items->Find(worn->second) : nullptr;
        row.Name    = "slot-" + slot.Name;
        row.Offset  = { 0.0f, height * static_cast<f32>(i) };
        row.Texture = Art.Row, row.Sampler = Art.Sampler;
        SetText(row.FindDescendant("slot"), InventoryScreen::NameOr("equipment.slot." + slot.Name, slot.Name));
        SetText(row.FindDescendant("item"), def ? Loc::Get(def->NameKey) : Loc::Get("ui.inventory.empty_slot"));
        SetIcon(row.FindDescendant("icon"), def ? def->Icon : std::string_view{});
        row.OnClick = [this, name = slot.Name] { Later([this, name] { Unequip(name); }); };
        row.CanDrop = [this, kind = slot.Kind](const DragPayload& p) {
            const ItemDef* item = p.Kind == "item" && Host.Items ? Host.Items->Find(std::any_cast<std::string>(p.Data)) : nullptr;
            return item && item->Slot == kind;
        };
        row.OnDrop = [this, name = slot.Name](const DragPayload& p) {
            const std::string item = std::any_cast<std::string>(p.Data);
            Later([this, name, item] {
                const bool worn = Host.Members->Equip(_member, item, name);
                const ItemDef* def = Host.Items ? Host.Items->Find(item) : nullptr;
                SetMessage(worn ? "ui.inventory.equipped" : "ui.inventory.cant_equip", { { "item", def ? Loc::Get(def->NameKey) : item } });
                Refresh();
            });
        };
    }
}

// "HP 80/120  MP 30/30  Attack 15 ...": resources with their maximum, which
// isn't listed again.
std::string InventoryScreen::MemberStats() const
{
    const PartyMember* member = Host.Members ? Host.Members->Find(_member) : nullptr;
    if (!member || !Host.Stats) return {};
    const StatValues values = Host.Members->GetStats(_member);
    std::string      text;
    for (const StatDef& def : Host.Stats->All()) {
        const bool isMax = std::ranges::any_of(Host.Stats->All(), [&](const StatDef& s) { return s.MaxStat == def.Id; });
        if (isMax || !values.contains(def.Id)) continue;
        std::string shown = Number(values.at(def.Id));
        if (!def.MaxStat.empty() && values.contains(def.MaxStat)) shown += "/" + Number(values.at(def.MaxStat));
        text += (text.empty() ? "" : "   ") + Loc::Get(def.NameKey) + " " + shown;
    }
    return text;
}

// The carry weight (bags with a limit), the currencies, the last message.
void InventoryScreen::ShowFooter()
{
    const Inventory* bag   = GetBag();
    const f32        limit = bag ? bag->GetRules().MaxWeight : 0.0f;
    if (auto* text = FindWidget<Label>("weight-text")) {
        text->Visible = limit > 0.0f;
        if (bag) text->Text = Loc::Format("ui.inventory.weight", { { "now", Number(bag->GetWeight()) }, { "max", Number(limit) } });
    }
    if (auto* bar = FindWidget<ProgressBar>("weight-bar")) {
        bar->Visible = limit > 0.0f;
        bar->Value   = limit > 0.0f && bag ? std::min(bag->GetWeight() / limit, 1.0f) : 0.0f;
        bar->SkinOverride.ProgressFill = bag && bag->IsOverWeight() ? std::optional(Color{ 0.85f, 0.30f, 0.30f, 1.0f }) : std::nullopt;
    }
    std::string money;
    if (Host.Money)
        for (const CurrencyDef& currency : Host.Money->GetAll())
            money += (money.empty() ? "" : "   ") + InventoryScreen::NameOr("currency." + currency.Id + ".name", currency.Id) +
                     std::format(" <color=gold>{}</color>", Host.Money->Get(currency.Id));
    SetText(FindWidget<Label>("currencies"), money);

    std::vector<LocArg> args;
    for (const auto& [name, value] : _messageArgs) args.emplace_back(name, value);
    _messageText = _messageKey.empty() ? std::string{} : Loc::Format(_messageKey, args);
    SetText(FindWidget<Label>("message"), _messageText);
}
} // namespace Arcbit
