#include <arcbit/ui/DiscardScreen.h>

#include <arcbit/core/Log.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <format>
#include <utility>

namespace Arcbit
{
DiscardScreen::DiscardScreen()
{
    BlocksInput     = true;
    TransitionSpeed = 6.0f;
}

// The suggestion: for each stack ConvertOverflow leaves out, entries of
// that item from the newest back until its count is covered.
void DiscardScreen::OnEnter()
{
    if (!_built) {
        if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "DiscardScreen: '{}' not found", LayoutPath);
        if (auto* panel = FindWidget<NineSlice>("panel")) panel->Texture = Art.Panel, panel->Sampler = Art.Sampler;
        if (auto* confirm = FindWidget<Button>("confirm")) confirm->OnClick = [this] { _later.push_back([this] { Confirm(); }); };
        if (auto* cancel = FindWidget<Button>("cancel")) cancel->OnClick = [this] { _later.push_back([this] { Cancel(); }); };
        _built = true;
        if (Bag)
            for (const ItemStack& out : Bag->ConvertOverflow(To)) {
                u32 covered = 0;
                for (u32 i = static_cast<u32>(Bag->GetStacks().size()); i-- > 0 && covered < out.Count;)
                    if (Bag->GetStacks()[i].Item == out.Item && std::ranges::find(_marked, i) == _marked.end()) {
                        _marked.push_back(i);
                        covered += Bag->GetStacks()[i].Count;
                    }
            }
        std::ranges::sort(_marked);
    }
    Show();
    FocusAt(0);
}

void DiscardScreen::Toggle(const u32 index)
{
    if (!Bag || index >= Bag->GetStacks().size()) return;
    if (const auto at = std::ranges::find(_marked, index); at != _marked.end()) _marked.erase(at);
    else _marked.insert(std::ranges::upper_bound(_marked, index), index);
    Show();
}

// A copy of the bag without the marked entries (and without telling anyone)
// fits the new rules.
bool DiscardScreen::CanConfirm() const
{
    if (!Bag) return false;
    Inventory rest = *Bag;
    rest.OnChange  = nullptr;
    for (auto it = _marked.rbegin(); it != _marked.rend(); ++it) (void)rest.RemoveAt(*it, rest.GetStacks()[*it].Count);
    return rest.ConvertOverflow(To).empty();
}

void DiscardScreen::Confirm()
{
    if (_done || !CanConfirm()) return;
    for (auto it = _marked.rbegin(); it != _marked.rend(); ++it) (void)Bag->RemoveAt(*it, Bag->GetStacks()[*it].Count);
    const bool changed = Bag->Convert(To);
    _done              = true;
    if (OnDone) OnDone(changed);
}

void DiscardScreen::Cancel()
{
    if (_done) return;
    _done = true;
    if (OnDone) OnDone(false);
}

void DiscardScreen::OnUpdate(f32)
{
    for (const auto& work : std::exchange(_later, {})) work();
}

// A row per entry, marked or not; the status line and Confirm follow
// whether what's left fits.
void DiscardScreen::Show()
{
    auto* list = FindWidget<ScrollPanel>("list");
    if (!list || !Bag) return;
    const auto& stacks = Bag->GetStacks();
    if (_rows.size() != stacks.size()) {
        list->ClearChildren();
        _rows.clear();
        for (UIWidget* made : AddFromTemplate("row", *list, static_cast<u32>(stacks.size())))
            if (auto* row = dynamic_cast<NineSliceButton*>(made)) _rows.push_back(row);
    }
    const f32 height    = GetMetaF32("row_height", 44.0f);
    list->ContentHeight = height * static_cast<f32>(_rows.size());
    for (u32 i = 0; i < _rows.size(); ++i) {
        NineSliceButton& row    = *_rows[i];
        const ItemDef*   def    = Items ? Items->Find(stacks[i].Item) : nullptr;
        const bool       marked = std::ranges::find(_marked, i) != _marked.end();
        row.Name       = std::format("row-{}", i);
        row.Offset     = { 0.0f, height * static_cast<f32>(i) };
        row.Texture    = Art.Row, row.Sampler = Art.Sampler;
        row.TintNormal = marked ? Color{ 0.55f, 0.22f, 0.22f, 1.0f } : Color{ 0.16f, 0.19f, 0.31f, 1.0f };
        row.OnClick    = [this, i] { _later.push_back([this, i] { Toggle(i); }); };
        if (auto* mark = dynamic_cast<Label*>(row.FindDescendant("mark"))) mark->Text = marked ? "X" : "";
        if (auto* name = dynamic_cast<Label*>(row.FindDescendant("name"))) name->Text = def ? Loc::Get(def->NameKey) : stacks[i].Item;
        if (auto* count = dynamic_cast<Label*>(row.FindDescendant("count"))) count->Text = stacks[i].Count > 1 ? std::format("x{}", stacks[i].Count) : "";
        if (auto* icon = dynamic_cast<Image*>(row.FindDescendant("icon"))) {
            const auto loaded = def && LoadIcon && !def->Icon.empty() ? LoadIcon(def->Icon) : std::nullopt;
            icon->Visible     = loaded.has_value();
            if (loaded) icon->Texture = loaded->Texture, icon->Sampler = loaded->Sampler, icon->UV = loaded->UV;
        }
    }
    const bool fits = CanConfirm();
    if (auto* status = FindWidget<Label>("status")) status->Text = Loc::Get(fits ? "ui.discard.fits" : "ui.discard.too_much");
    if (auto* confirm = FindWidget<Button>("confirm")) confirm->Enabled = fits;
    if (auto* message = FindWidget<Label>("message")) message->Text = Loc::Format("ui.discard.message", { { "count", static_cast<i64>(_marked.size()) } });
}
} // namespace Arcbit
