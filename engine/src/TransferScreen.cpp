#include <arcbit/ui/TransferScreen.h>

#include <arcbit/core/Log.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <format>
#include <utility>

namespace Arcbit
{
namespace
{
    // A row dragged from one side (DragPayload::Data, Kind "transfer").
    struct TransferDrag
    {
        TransferScreen::Side From;
        u32                  Index;
    };

    constexpr const char* ListNames[2] = { "from", "to" };
} // namespace

TransferScreen::TransferScreen()
{
    BlocksInput     = true;
    TransitionSpeed = 6.0f;
}

Inventory* TransferScreen::SideBag(const Side side) const
{
    if (side == Side::Bag) return Host.Bags ? Host.Bags->Find(Bag) : nullptr;
    Container* container = Host.Container ? Host.Container() : nullptr;
    return container ? &container->Items : nullptr;
}

void TransferScreen::OnEnter()
{
    if (!_built) {
        if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "TransferScreen: '{}' not found", LayoutPath);
        if (auto* panel = FindWidget<NineSlice>("panel")) panel->Texture = Art.Panel, panel->Sampler = Art.Sampler;
        if (auto* take = FindWidget<Button>("take-all")) take->OnClick = [this] { Later([this] { TakeAll(); }); };
        for (const Side side : { Side::Container, Side::Bag }) {
            auto* list = FindWidget<ScrollPanel>(ListNames[static_cast<int>(side)]);
            if (!list) continue;
            list->CanDrop = [side](const DragPayload& p) {
                const auto* drag = p.Kind == "transfer" ? std::any_cast<TransferDrag>(&p.Data) : nullptr;
                return drag && drag->From != side;
            };
            list->OnDrop = [this](const DragPayload& p) {
                const TransferDrag drag = std::any_cast<TransferDrag>(p.Data);
                Later([this, drag, one = p.Count == 1] { (void)Move(drag.From, drag.Index, one); });
            };
        }
        _built = true;
    }
    Refresh();
    FocusAt(0);
}

// Each side's rows; the container's currency; titles.
void TransferScreen::Refresh()
{
    if (auto* title = FindWidget<Label>("title")) title->Text = Title.empty() ? Loc::Get("ui.transfer.title") : Title;
    FillSide(Side::Container);
    FillSide(Side::Bag);
    const Container* container = Host.Container ? Host.Container() : nullptr;
    std::string      money;
    if (container)
        for (const auto& [id, amount] : container->Currency)
            if (amount != 0)
                money += (money.empty() ? "" : "   ") + InventoryScreen::NameOr("currency." + id + ".name", id) + std::format(" <color=gold>{}</color>", amount);
    if (auto* label = FindWidget<Label>("money")) label->Text = money;
    if (auto* label = FindWidget<Label>("message")) label->Text = _message;
    _signature = Signature();
}

// A side's rows - made when their number changes, focus kept on a row.
void TransferScreen::FillSide(const Side side)
{
    auto*            list = FindWidget<ScrollPanel>(ListNames[static_cast<int>(side)]);
    const Inventory* bag  = SideBag(side);
    auto&            rows = _rows[static_cast<int>(side)];
    const size_t     n    = bag ? bag->GetStacks().size() : 0;
    if (!list) return;
    if (rows.size() != n) {
        const i32 focus = GetFocusIndex();
        list->ClearChildren();
        rows.clear();
        for (UIWidget* made : AddFromTemplate("row", *list, static_cast<u32>(n)))
            if (auto* row = dynamic_cast<NineSliceButton*>(made)) rows.push_back(row);
        if (focus >= 0) FocusAt(focus);
    }
    const f32 height = GetMetaF32("row_height", 44.0f);
    list->ContentHeight = height * static_cast<f32>(rows.size());
    for (u32 i = 0; i < rows.size(); ++i) {
        const ItemStack& stack = bag->GetStacks()[i];
        const ItemDef*   def   = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
        NineSliceButton& row   = *rows[i];
        row.Name    = std::format("{}-{}", ListNames[static_cast<int>(side)], i);
        row.Offset  = { 0.0f, height * static_cast<f32>(i) };
        row.Texture = Art.Row, row.Sampler = Art.Sampler;
        if (auto* well = dynamic_cast<Image*>(row.FindDescendant("slot"))) well->Texture = Art.Slot, well->Sampler = Art.Sampler;
        if (auto* icon = dynamic_cast<Image*>(row.FindDescendant("icon"))) {
            const auto loaded = def && Host.LoadIcon && !def->Icon.empty() ? Host.LoadIcon(def->Icon) : std::nullopt;
            icon->Visible     = loaded.has_value();
            if (loaded) icon->Texture = loaded->Texture, icon->Sampler = loaded->Sampler, icon->UV = loaded->UV;
        }
        if (auto* name = dynamic_cast<Label*>(row.FindDescendant("name"))) name->Text = def ? Loc::Get(def->NameKey) : stack.Item;
        if (auto* count = dynamic_cast<Label*>(row.FindDescendant("count"))) count->Text = stack.Count > 1 ? std::format("x{}", stack.Count) : "";
        row.TooltipKey = def ? def->DescriptionKey : std::string{};
        WireRow(row, side, i);
    }
}

void TransferScreen::WireRow(UIWidget& row, const Side side, const u32 index)
{
    dynamic_cast<NineSliceButton&>(row).OnClick = [this, side, index] { Later([this, side, index] { (void)Move(side, index, _shift); }); };
    row.OnDragStart = [this, side, index](const DragStart& how) -> std::optional<DragPayload> {
        const Inventory* bag = SideBag(side);
        if (!bag || index >= bag->GetStacks().size()) return std::nullopt;
        const u32 count = how.Alternate ? 1 : bag->GetStacks()[index].Count;
        return DragPayload{ .Kind = "transfer", .Data = TransferDrag{ side, index }, .Count = count,
                            .Tint = { 0.30f, 0.36f, 0.56f, 1.0f }, .Size = { 40.0f, 40.0f }, .Text = std::format("{}", count) };
    };
}

u32 TransferScreen::Move(const Side from, const u32 index, const bool one)
{
    Inventory* source = SideBag(from);
    Inventory* target = SideBag(from == Side::Bag ? Side::Container : Side::Bag);
    if (!source || !target || index >= source->GetStacks().size()) return 0;
    const ItemStack stack = source->GetStacks()[index];
    const u32       moved = target->Add(stack.Item, one ? 1 : stack.Count);
    (void)source->RemoveAt(index, moved);
    const ItemDef* def = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
    _message = moved == 0 ? Loc::Format("ui.transfer.no_room", { { "item", def ? Loc::Get(def->NameKey) : stack.Item } }) : std::string{};
    Refresh();
    return moved;
}

// Everything that fits into the bag, oldest entry first; the currency too.
void TransferScreen::TakeAll()
{
    Container* container = Host.Container ? Host.Container() : nullptr;
    Inventory* bag       = SideBag(Side::Bag);
    if (!container || !bag) return;
    for (const ItemStack& stack : std::vector<ItemStack>(container->Items.GetStacks()))
        (void)container->Items.Remove(stack.Item, bag->Add(stack.Item, stack.Count));
    if (Host.Money)
        for (const auto& [id, amount] : container->Currency) (void)Host.Money->Add(id, amount);
    container->Currency.clear();
    _message = Loc::Get(container->Items.IsEmpty() ? "ui.transfer.took_all" : "ui.transfer.took_some");
    Refresh();
}

void TransferScreen::OnTick(f32, const InputManager& input) { _shift = input.IsPressed(UIManager::ActionShiftMod); }

// The work callbacks asked for; closed if the container went; redrawn if
// either side changed under it.
void TransferScreen::OnUpdate(f32)
{
    for (const auto& work : std::exchange(_later, {})) work();
    if (!_built) return;
    if (!Host.Container || !Host.Container()) {
        if (OnClose) OnClose();
        return;
    }
    if (Signature() != _signature) Refresh();
}

void TransferScreen::OnBackPressed()
{
    if (OnClose) OnClose();
}

u64 TransferScreen::Signature() const
{
    u64        hash = 1469598103934665603ull;
    const auto mix  = [&hash](const std::string& text) {
        for (const char c : text) hash = (hash ^ static_cast<u8>(c)) * 1099511628211ull;
        hash = (hash ^ 0xFF) * 1099511628211ull;
    };
    for (const Side side : { Side::Container, Side::Bag })
        if (const Inventory* bag = SideBag(side))
            for (const ItemStack& stack : bag->GetStacks()) mix(std::format("{}{}", stack.Item, stack.Count));
    if (const Container* container = Host.Container ? Host.Container() : nullptr)
        for (const auto& [id, amount] : container->Currency) mix(std::format("{}{}", id, amount));
    return hash;
}
} // namespace Arcbit
