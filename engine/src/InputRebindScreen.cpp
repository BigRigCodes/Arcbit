#include <arcbit/ui/InputRebindScreen.h>

#include <arcbit/settings/Settings.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/input/InputManager.h>

#include <algorithm>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Module-scope helpers
// ---------------------------------------------------------------------------

static bool HasHiddenPrefix(const std::string_view name, const std::vector<std::string>& prefixes)
{
    return std::ranges::any_of(prefixes, [&](const std::string& p) { return name.starts_with(p); });
}

static bool IsKBM(const Binding& b)
{
    return b.BindingType == Binding::Type::Key ||
            b.BindingType == Binding::Type::MouseButton;
}

// ---------------------------------------------------------------------------
// BuildEntries — collect + sort visible actions once on enter.
// ---------------------------------------------------------------------------

void InputRebindScreen::BuildEntries()
{
    _entries.clear();
    for (const ActionID id : Input->GetAllActions()) {
        const std::string_view name = Input->GetActionName(id);
        if (name.empty() || HasHiddenPrefix(name, HiddenPrefixes)) continue;
        _entries.push_back({
            id,
            std::string(Input->GetActionDisplayName(id)),
            std::string(Input->GetActionCategory(id))
        });
    }
    std::ranges::sort(_entries, [](const ActionEntry& a, const ActionEntry& b) {
        return a.category != b.category ? a.category < b.category : a.display < b.display;
    });
}

// ---------------------------------------------------------------------------
// OnEnter — build fixed chrome; call RebuildScroll for the dynamic rows.
// ---------------------------------------------------------------------------

void InputRebindScreen::OnEnter()
{
    _roots.clear();
    _isListening     = false;
    _listeningButton = nullptr;
    _hadRemoved      = false;

    if (!Input) return;
    BuildEntries();

    LoadLayout(LayoutPath);

    _scroll = FindWidget<ScrollPanel>("scroll");
    RebuildScroll();

    if (auto* back = FindWidget<Button>("back-btn"))
        back->OnClick = [this] {
            StopListening(false);
            if (OnBack) OnBack();
        };
}

// ---------------------------------------------------------------------------
// CountBindings / AddChipColumn helpers
// ---------------------------------------------------------------------------

i32 InputRebindScreen::CountBindings(const ActionID action, const bool forKey) const
{
    i32 n = 0;
    for (const Binding& b : Input->GetBindings(action))
        if (IsKBM(b) == forKey) ++n;
    return n;
}

void InputRebindScreen::AddChipColumn(const ActionID action, const bool forKey,
                                      const f32      colX, const f32    startY,
                                      const f32      chipH, const f32   chipGap)
{
    const f32 labelW   = 148.0f;
    const f32 xBtnW    = 22.0f;
    const f32 xBtnX    = colX + labelW + 3.0f;
    const f32 chipSlot = chipH + chipGap;
    i32       slot     = 0;

    for (const Binding& b : Input->GetBindings(action)) {
        if (IsKBM(b) != forKey) continue;
        const f32     y   = startY + slot * chipSlot;
        const Binding cap = b;

        auto* chipBtn    = _scroll->AddChild<Button>();
        chipBtn->Text    = InputManager::BindingToString(cap);
        chipBtn->Size    = {labelW, chipH};
        chipBtn->Anchor  = {0.0f, 0.0f};
        chipBtn->Offset  = {colX, y};
        chipBtn->ZOrder  = 4;
        chipBtn->OnClick = [this, action, forKey, cap, chipBtn]() {
            StartListening(action, forKey, chipBtn, true, cap);
        };

        auto* xBtn    = _scroll->AddChild<Button>();
        xBtn->Text    = "x";
        xBtn->Size    = {xBtnW, chipH};
        xBtn->Anchor  = {0.0f, 0.0f};
        xBtn->Offset  = {xBtnX, y};
        xBtn->ZOrder  = 4;
        xBtn->OnClick = [this, action, cap]() { RemoveChip(action, cap); };

        ++slot;
    }

    // "+" button to add a new binding
    auto* addBtn    = _scroll->AddChild<Button>();
    addBtn->Text    = "+";
    addBtn->Size    = {labelW, chipH};
    addBtn->Anchor  = {0.0f, 0.0f};
    addBtn->Offset  = {colX, startY + slot * chipSlot};
    addBtn->ZOrder  = 4;
    addBtn->OnClick = [this, action, forKey, addBtn]() { StartListening(action, forKey, addBtn, false); };
}

// ---------------------------------------------------------------------------
// RebuildScroll — clear and repopulate scroll contents; preserves offset.
// ---------------------------------------------------------------------------

void InputRebindScreen::RebuildScroll()
{
    if (!_scroll) return;
    const f32 savedOffset = _scroll->ScrollOffset;
    const i32 focusIndex  = GetFocusIndex(); // the rows are rebuilt; keep the controller's place
    _scroll->ClearChildren();

    const f32 chipH    = 22.0f;
    const f32 chipGap  = 4.0f;
    const f32 chipSlot = chipH + chipGap;
    const f32 rowPad   = 10.0f;
    const f32 rowW     = _scroll->Size.X - _scroll->ScrollbarWidth - 8.0f;
    const f32 nameX    = GetMetaF32("name_x", 8.0f);
    const f32 keyColX  = GetMetaF32("key_col_x", 242.0f);
    const f32 ctrlColX = GetMetaF32("ctrl_col_x", 454.0f);

    f32              y = 4.0f;
    std::string_view lastCategory;

    for (const ActionEntry& e : _entries) {
        if (e.category != lastCategory) {
            if (!e.category.empty()) {
                if (y > 4.0f) y += 6.0f; // extra gap before non-first category

                auto* bar                 = _scroll->AddChild<Panel>();
                bar->Size                 = {rowW, 24.0f};
                bar->Anchor               = {0.0f, 0.0f};
                bar->Offset               = {0.0f, y};
                bar->ZOrder               = 3;
                bar->SkinOverride.PanelBg = {0.22f, 0.30f, 0.45f, 1.0f};

                auto* cat                   = _scroll->AddChild<Label>();
                cat->Text                   = e.category;
                cat->Size                   = {rowW, 24.0f};
                cat->Anchor                 = {0.0f, 0.0f};
                cat->Offset                 = {nameX + 4.0f, y};
                cat->ZOrder                 = 4;
                cat->SkinOverride.TextLabel = {0.85f, 0.92f, 1.0f, 1.0f};
                cat->AutoCenter             = true;

                y += 30.0f;
            }
            lastCategory = e.category;
        }

        if (y > 4.0f) {
            auto* sep   = _scroll->AddChild<Panel>();
            sep->Size   = {rowW - 8.0f, 1.0f};
            sep->Anchor = {0.0f, 0.0f};
            sep->Offset = {4.0f, y};
            sep->ZOrder = 3;
        }

        const i32 keySlots  = CountBindings(e.id, true) + 1; // +1 for "+"
        const i32 ctrlSlots = CountBindings(e.id, false) + 1;
        const f32 rowH      = std::max(keySlots, ctrlSlots) * chipSlot - chipGap + rowPad * 2.0f;

        auto* nameLabel   = _scroll->AddChild<Label>();
        nameLabel->Text   = e.display;
        nameLabel->Size   = {226.0f, rowH};
        nameLabel->Anchor = {0.0f, 0.0f};
        nameLabel->Offset = {nameX, y};
        nameLabel->ZOrder = 4;

        AddChipColumn(e.id, true, keyColX, y + rowPad, chipH, chipGap);
        AddChipColumn(e.id, false, ctrlColX, y + rowPad, chipH, chipGap);

        y += rowH;
    }

    _scroll->ContentHeight = y + 4.0f;
    _scroll->ScrollOffset  = std::min(savedOffset,
                                      std::max(0.0f, _scroll->ContentHeight - _scroll->Size.Y));
    FocusAt(focusIndex);
}

// ---------------------------------------------------------------------------
// Listening mode
// ---------------------------------------------------------------------------

void InputRebindScreen::StartListening(const ActionID action, const bool forKey,
                                       Button* const  btn, const bool    replaceExisting,
                                       const Binding  toReplace)
{
    // Clicking the same slot again cancels.
    if (_isListening && _listeningButton == btn) {
        StopListening(false);
        return;
    }

    // Restore any previously removed binding without rebuilding the scroll.
    CancelListening();

    _isListening     = true;
    _listeningAction = action;
    _listeningForKey = forKey;
    _listeningButton = btn;
    _flashTimer      = 0.0f;
    _hadRemoved      = replaceExisting;

    if (replaceExisting) {
        _removedBinding = toReplace;
        Input->RemoveBinding(action, toReplace);
    }

    btn->Text = "...";
}

void InputRebindScreen::CancelListening()
{
    if (!_isListening) return;
    if (_hadRemoved) Input->AddBinding(_listeningAction, _removedBinding);
    _isListening     = false;
    _listeningButton = nullptr;
    _hadRemoved      = false;
    _flashTimer      = 0.0f;
}

void InputRebindScreen::StopListening(const bool commit)
{
    if (!_isListening && !_hadRemoved) {
        _pendingRebuild = true;
        return;
    }
    if (!commit && _hadRemoved) Input->AddBinding(_listeningAction, _removedBinding);
    if (commit) Settings::MarkDirty("input");
    _isListening     = false;
    _listeningButton = nullptr;
    _hadRemoved      = false;
    _flashTimer      = 0.0f;
    _pendingRebuild  = true;
}

void InputRebindScreen::RemoveChip(const ActionID action, const Binding& b)
{
    CancelListening(); // restores any in-flight removal, no rebuild yet
    Input->RemoveBinding(action, b);
    Settings::MarkDirty("input");
    _pendingRebuild = true;
}

// ---------------------------------------------------------------------------
// OnTick — flash listening button; capture released input.
// ---------------------------------------------------------------------------

void InputRebindScreen::OnTick(const f32 dt, const InputManager& input)
{
    if (_pendingRebuild) {
        _pendingRebuild = false;
        RebuildScroll();
    }

    if (!_isListening) return;

    _flashTimer += dt;
    if (_listeningButton)
        _listeningButton->Text = static_cast<i32>(_flashTimer * 3.0f) % 2 == 0 ? "..." : "   ";

    if (_listeningForKey) {
        const Key         k  = input.GetAnyJustReleasedKey();
        const MouseButton mb = input.GetAnyJustReleasedMouseButton();
        if (k == Key::Unknown && mb == MouseButton::Count) return;

        TakeBinding(k != Key::Unknown ? Binding::FromKey(k) : Binding::FromMouseButton(mb));
    }
    else {
        const GamepadButton btn = input.GetAnyJustReleasedGamepadButton();
        if (btn == GamepadButton::Count) return;
        TakeBinding(Binding::FromGamepadButton(btn));
    }

    StopListening(true);
}

// A binding belongs to one of the actions on this screen at a time, so it
// moves from whichever had it. Only those, though: the hidden UI_ / Engine_
// actions keep theirs - binding A to Interact used to take A away from menu
// confirm, and the D-pad from menu navigation.
void InputRebindScreen::TakeBinding(const Binding& b)
{
    for (const ActionEntry& e : _entries) Input->RemoveBinding(e.id, b);
    Input->AddBinding(_listeningAction, b);
}

void InputRebindScreen::OnBackPressed()
{
    // Cancel any in-flight listen before navigating away.
    if (_isListening) {
        StopListening(false);
        return;
    }
    if (OnBack) OnBack();
}
} // namespace Arcbit
