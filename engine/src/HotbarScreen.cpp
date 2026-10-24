#include <arcbit/ui/HotbarScreen.h>

#include <arcbit/core/Log.h>
#include <arcbit/items/Inventory.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/Widgets.h>

#include <format>

namespace Arcbit
{
HotbarScreen::HotbarScreen()
{
    Layer           = UILayer::Hud;
    TakesInput      = false;
    TransitionSpeed = 0.0f;
}

std::vector<std::string> HotbarScreen::GetSlots() const
{
    const Inventory* bag = Host.Bag ? Host.Bag() : nullptr;
    if (!bag) return {};
    if (!bag->GetQuickSlots().empty()) return bag->GetQuickSlots();
    std::vector<std::string> slots;
    for (const InventorySection& section : bag->GetRules().Sections) {
        if (section.Name != Section) continue;
        for (u32 y = 0; y < section.Height; ++y)
            for (u32 x = 0; x < section.Width; ++x) {
                const std::optional<u32> on = bag->StackAt(Section, x, y);
                slots.push_back(on ? bag->GetStacks()[*on].Item : std::string{});
            }
    }
    return slots;
}

void HotbarScreen::Select(const u32 slot)
{
    const size_t count = GetSlots().size();
    if (count > 0) _selected = std::min<u32>(slot, static_cast<u32>(count - 1));
}

void HotbarScreen::Step(const i32 by)
{
    const i32 count = static_cast<i32>(GetSlots().size());
    if (count > 0) _selected = static_cast<u32>(((static_cast<i32>(_selected) + by) % count + count) % count);
}

std::string HotbarScreen::GetSelectedItem() const
{
    const std::vector<std::string> slots = GetSlots();
    return _selected < slots.size() ? slots[_selected] : std::string{};
}

void HotbarScreen::OnEnter()
{
    if (_built) return;
    if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "HotbarScreen: '{}' not found", LayoutPath);
    _built = true;
}

void HotbarScreen::OnUpdate(f32)
{
    if (!_built) return;
    const bool shown = !Host.Shown || Host.Shown();
    if (auto* bar = FindWidget<UIWidget>("bar")) bar->Visible = shown;
    if (shown) Show();
}

// A view per slot, left to right: the item's icon (its name without one),
// the key that picks it, how many the bag holds; the selected one lit.
void HotbarScreen::Show()
{
    auto* bar = FindWidget<UIWidget>("bar");
    if (!bar) return;
    const std::vector<std::string> slots = GetSlots();
    const Inventory*               bag   = Host.Bag ? Host.Bag() : nullptr;
    if (_slotViews.size() != slots.size()) {
        bar->ClearChildren();
        _slotViews = AddFromTemplate("slot", *bar, static_cast<u32>(slots.size()));
    }
    const f32 width = _slotViews.empty() ? 0.0f : _slotViews.front()->Size.X, gap = GetMetaF32("gap", 6.0f);
    bar->Size.X     = static_cast<f32>(_slotViews.size()) * (width + gap) - gap;
    for (u32 i = 0; i < _slotViews.size(); ++i) {
        UIWidget&      view = *_slotViews[i];
        const ItemDef* def  = Host.Items && !slots[i].empty() ? Host.Items->Find(slots[i]) : nullptr;
        const auto     icon = def && Host.LoadIcon && !def->Icon.empty() ? Host.LoadIcon(def->Icon) : std::nullopt;
        view.Offset         = { static_cast<f32>(i) * (width + gap), 0.0f };
        if (auto* panel = dynamic_cast<Panel*>(&view))
            panel->SkinOverride.PanelBg = i == _selected ? Color{ 0.55f, 0.45f, 0.22f, 0.9f } : Color{ 0.10f, 0.12f, 0.20f, 0.8f };
        if (auto* image = dynamic_cast<Image*>(view.FindDescendant("icon"))) {
            image->Visible = icon.has_value();
            if (icon) image->Texture = icon->Texture, image->Sampler = icon->Sampler, image->UV = icon->UV;
        }
        if (auto* name = dynamic_cast<Label*>(view.FindDescendant("name"))) name->Text = def && !icon ? Loc::Get(def->NameKey) : "";
        if (auto* key = dynamic_cast<Label*>(view.FindDescendant("key"))) key->Text = i < ActionSlots.size() ? std::format("{}", i + 1) : "";
        const u32 count = def && bag ? bag->Count(def->Id) : 0;
        if (auto* label = dynamic_cast<Label*>(view.FindDescendant("count"))) label->Text = def && count != 1 ? std::format("{}", count) : "";
        view.Opacity = def && count == 0 ? 0.5f : 1.0f; // a slot whose item ran out keeps it, dimmed
    }
}
} // namespace Arcbit
