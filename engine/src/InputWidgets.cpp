#include <arcbit/ui/InputWidgets.h>
#include <arcbit/core/Clipboard.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/core/Loc.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/render/Font.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Utf8.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>

namespace { void PlaySound(const std::string& key) { if (!key.empty()) Arcbit::AudioManager::PlayOneShot(key, 1.0f, Arcbit::AudioCategories::Ui); } }

namespace Arcbit
{
namespace
{
// Keys -> their strings, for widgets with item keys.
void Resolve(const std::vector<std::string>& keys, std::vector<std::string>& items)
{
    if (keys.empty()) return;
    items.clear();
    for (const std::string& key : keys) items.push_back(Loc::Get(key));
}
} // namespace

void TextInput::OnLocaleChanged()
{
    if (!PlaceholderKey.empty()) Placeholder = Loc::Get(PlaceholderKey);
}

void Dropdown::OnLocaleChanged() { Resolve(ItemKeys, Items); }

void RadioGroup::OnLocaleChanged() { Resolve(ItemKeys, Items); }

void Checkbox::SetLabelKey(std::string key)
{
    LabelKey = std::move(key);
    OnLocaleChanged();
}

void Checkbox::OnLocaleChanged()
{
    if (!LabelKey.empty()) Label = Loc::Get(LabelKey);
}

// ---------------------------------------------------------------------------
// Rendering helpers (mirrors Widgets.cpp — not shared to avoid coupling)
// ---------------------------------------------------------------------------

static i32 BgLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerBg;
}

static i32 FillLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerFill;
}

static i32 TextLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerText;
}

static void PushRect(FramePacket&        packet, const UIRect&    r, const Color& c,
                     const TextureHandle tex, const SamplerHandle samp, const i32 layer)
{
    if (r.W <= 0.0f || r.H <= 0.0f) return;
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = {r.X + r.W * 0.5f, r.Y + r.H * 0.5f};
    s.Size     = {r.W, r.H};
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

static Color WithAlpha(Color c, const f32 alpha)
{
    c.A *= alpha;
    return c;
}

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

bool TextInput::AcceptsChar(const u32 codepoint) const
{
    if (codepoint < 32 || codepoint == 127) return false; // control characters
    switch (InputMode) {
    case Mode::Text: return true;
    case Mode::Numeric: return (codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-';
    case Mode::Regex: if (Pattern.empty()) return true;
        try {
            const std::regex re(Pattern);
            std::string      ch;
            Utf8::Append(ch, codepoint);
            return std::regex_match(ch, re);
        }
        catch (...) { return false; }
    }
    return false;
}

void TextInput::DeleteSelection()
{
    if (!HasSelection()) return;
    const u32 lo = SelMin(), hi = SelMax();
    Text.erase(lo, hi - lo);
    _cursor = _selAnchor = lo;
}

void TextInput::SetText(std::string text)
{
    Text       = std::move(text);
    _cursor    = static_cast<u32>(Text.size());
    _selAnchor = _cursor;
    _scrollX   = 0.0f;
}

void TextInput::ClampCursor()
{
    const u32 size = static_cast<u32>(Text.size());
    // Back off continuation bytes, so both land on a character's start.
    const auto onChar = [this](u32 pos) {
        while (pos > 0 && (static_cast<u8>(Text[pos]) & 0xC0) == 0x80) --pos;
        return pos;
    };
    _cursor    = onChar(std::min(_cursor, size));
    _selAnchor = onChar(std::min(_selAnchor, size));
}

void TextInput::MoveCursor(const u32 pos, const bool extendSel)
{
    _cursor = static_cast<u32>(std::min(static_cast<usize>(pos), Text.size()));
    if (!extendSel) _selAnchor = _cursor;
}

u32 TextInput::PrevChar() const { return static_cast<u32>(Utf8::Prev(Text, _cursor)); }
u32 TextInput::NextChar() const { return static_cast<u32>(Utf8::Next(Text, _cursor)); }

u32 TextInput::PrevWordBoundary() const
{
    if (_cursor == 0) return 0;
    u32 i = _cursor - 1;
    while (i > 0 && Text[i] == ' ') --i;
    while (i > 0 && Text[i - 1] != ' ') --i;
    return i;
}

u32 TextInput::NextWordBoundary() const
{
    const u32 n = static_cast<u32>(Text.size());
    u32       i = _cursor;
    while (i < n && Text[i] != ' ') ++i;
    while (i < n && Text[i] == ' ') ++i;
    return i;
}

void TextInput::OnTextInput(const std::string_view chars)
{
    if (!_focused) return;
    ClampCursor();
    DeleteSelection(); // replace selected text
    usize length = Utf8::Length(Text);
    for (const u32 cp : Utf8::Codepoints(chars)) {
        if (!AcceptsChar(cp)) continue;
        if (length >= MaxLength) break;
        std::string encoded;
        Utf8::Append(encoded, cp);
        Text.insert(_cursor, encoded);
        _cursor += static_cast<u32>(encoded.size());
        ++length;
    }
    _selAnchor = _cursor;
    if (OnChanged) OnChanged(Text);
}

void TextInput::OnControlKey(const UIControlKey key)
{
    ClampCursor();
    switch (key) {
    // Plain motion — clears selection
    case UIControlKey::Left:
        if (HasSelection()) MoveCursor(SelMin(), false);
        else if (_cursor > 0) MoveCursor(PrevChar(), false);
        break;
    case UIControlKey::Right:
        if (HasSelection()) MoveCursor(SelMax(), false);
        else MoveCursor(NextChar(), false);
        break;
    case UIControlKey::Home: MoveCursor(0, false);
        break;
    case UIControlKey::End: MoveCursor(static_cast<u32>(Text.size()), false);
        break;

    // Shift motion — extends selection
    case UIControlKey::ShiftLeft: if (_cursor > 0) MoveCursor(PrevChar(), true);
        break;
    case UIControlKey::ShiftRight: MoveCursor(NextChar(), true);
        break;
    case UIControlKey::ShiftHome: MoveCursor(0, true);
        break;
    case UIControlKey::ShiftEnd: MoveCursor(static_cast<u32>(Text.size()), true);
        break;

    // Ctrl word navigation
    case UIControlKey::CtrlLeft: MoveCursor(PrevWordBoundary(), false);
        break;
    case UIControlKey::CtrlRight: MoveCursor(NextWordBoundary(), false);
        break;
    case UIControlKey::CtrlShiftLeft: MoveCursor(PrevWordBoundary(), true);
        break;
    case UIControlKey::CtrlShiftRight: MoveCursor(NextWordBoundary(), true);
        break;

    case UIControlKey::Backspace:
        if (HasSelection()) {
            DeleteSelection();
            if (OnChanged) OnChanged(Text);
        }
        else if (_cursor > 0) {
            const u32 start = PrevChar();
            Text.erase(start, _cursor - start);
            _cursor = _selAnchor = start;
            if (OnChanged) OnChanged(Text);
        }
        break;
    case UIControlKey::Delete:
        if (HasSelection()) {
            DeleteSelection();
            if (OnChanged) OnChanged(Text);
        }
        else if (_cursor < Text.size()) {
            Text.erase(_cursor, NextChar() - _cursor);
            if (OnChanged) OnChanged(Text);
        }
        break;

    case UIControlKey::Enter: if (OnConfirm) OnConfirm(Text);
        break;
    case UIControlKey::SelectAll:
    case UIControlKey::Copy:
    case UIControlKey::Cut:
    case UIControlKey::Paste: UseClipboard(key);
        break;
    default: break;
    }
    _cursorBlink = 0.0f; // reset blink so cursor is always visible right after moving
}

// Select all, copy and cut the selection, paste over it. A paste is typed
// in - the field's own rules (mode, pattern, length) apply - with its line
// breaks as spaces: a text field is one line.
void TextInput::UseClipboard(const UIControlKey key)
{
    if (key == UIControlKey::SelectAll) {
        _selAnchor = 0;
        _cursor    = static_cast<u32>(Text.size());
        return;
    }
    if (key == UIControlKey::Paste) {
        std::string pasted = Clipboard::GetText();
        std::erase(pasted, '\r');
        std::ranges::replace(pasted, '\n', ' ');
        if (!pasted.empty()) OnTextInput(pasted);
        return;
    }
    if (!HasSelection()) return;
    Clipboard::SetText(std::string_view(Text).substr(SelMin(), SelMax() - SelMin()));
    if (key != UIControlKey::Cut) return;
    DeleteSelection();
    if (OnChanged) OnChanged(Text);
}

void TextInput::OnUpdate(const f32 dt, const UIRect myRect, const Vec2 /*mousePos*/,
                         const bool /*mouseDown*/, const bool /*mouseJustDown*/,
                         const bool /*mouseJustUp*/, bool& /*consumed*/)
{
    if (!Enabled) return;
    _cursorBlink += dt;
    if (_cursorBlink >= 1.0f) _cursorBlink -= 1.0f;
}

void TextInput::UpdateScroll(const f32 availW, const UISkin& skin)
{
    if (!skin.Font) return;
    const f32 cursorX = MeasureTextWidth(*skin.Font, Text.substr(0, _cursor), skin.FontScale);
    const f32 textW   = MeasureTextWidth(*skin.Font, Text, skin.FontScale);
    if (cursorX - _scrollX > availW) _scrollX = cursorX - availW;
    if (cursorX - _scrollX < 0.0f) _scrollX = cursorX;
    _scrollX = std::clamp(_scrollX, 0.0f, std::max(0.0f, textW - availW));
}

void TextInput::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                          const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                          const UISkin&       skin)
{
    ClampCursor();
    const Color borderColor = _focused ? skin.InputFocusBorder : skin.InputBorder;
    // Draw border rect slightly outside, then background on top
    const UIRect border = {myRect.X - 1, myRect.Y - 1, myRect.W + 2, myRect.H + 2};
    PushRect(packet, border, WithAlpha(borderColor, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin) - 1);
    PushRect(packet, myRect, WithAlpha(skin.InputBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    if (!skin.Font) return;

    const f32 pad    = 6.0f;
    const f32 availW = myRect.W - pad * 2.0f;
    UpdateScroll(availW, skin);

    // Text Y: center the full line-height (ascent + descent) within the widget.
    // Using GetLineHeight() ensures descenders fit without clipping.
    const f32 lineH = skin.Font->GetLineHeight() * skin.FontScale;
    const f32 textY = myRect.Y + (myRect.H - lineH) * 0.5f;
    const f32 textX = myRect.X + pad - _scrollX;

    // Register clip rect before emitting any text or selection sprites.
    // Horizontal clipping is tight to the content area; vertical has generous
    // slack to accommodate SDF glyph padding without shaving descenders.
    const f32 vslack = lineH * 0.5f;
    packet.UIClipRects.push_back({myRect.X + pad, myRect.Y - vslack, availW, myRect.H + vslack * 2.0f});
    const u16 clipIdx = static_cast<u16>(packet.UIClipRects.size()); // 1-based

    auto clip = [&](size_t from) {
        for (size_t i = from; i < packet.UISprites.size(); ++i)
            if (packet.UISprites[i].ClipIndex == 0)
                packet.UISprites[i].ClipIndex = clipIdx;
    };

    // Selection highlight
    if (_focused && HasSelection()) {
        const f32 x0 = myRect.X + pad + MeasureTextWidth(*skin.Font, Text.substr(0, SelMin()), skin.FontScale) -
                _scrollX;
        const f32 x1 = myRect.X + pad + MeasureTextWidth(*skin.Font, Text.substr(0, SelMax()), skin.FontScale) -
                _scrollX;
        const UIRect selRect = {x0, myRect.Y + 2.0f, x1 - x0, myRect.H - 4.0f};
        const size_t mark    = packet.UISprites.size();
        PushRect(packet, selRect, WithAlpha(skin.AccentColor, effectiveOpacity * 0.4f),
                 whiteTex, whiteSampler, FillLayer(*this, skin));
        clip(mark);
    }

    // Text or placeholder
    const bool  showPlaceholder = Text.empty() && !_focused;
    const Color textColor       = showPlaceholder
                                  ? WithAlpha(skin.InputPlaceholder, effectiveOpacity)
                                  : WithAlpha(skin.TextNormal, effectiveOpacity);
    const std::string_view display = showPlaceholder ? std::string_view(Placeholder) : std::string_view(Text);
    {
        const size_t mark = packet.UISprites.size();
        DrawTextUI(packet, *skin.Font, display, {textX, textY}, skin.FontScale, textColor, TextLayer(*this, skin));
        clip(mark);
    }

    // The suggestion, dimmed, right after the text.
    if (_focused && !Suggestion.empty() && _cursor == Text.size()) {
        const f32    sx   = textX + MeasureTextWidth(*skin.Font, Text, skin.FontScale);
        const size_t mark = packet.UISprites.size();
        DrawTextUI(packet, *skin.Font, Suggestion, {sx, textY}, skin.FontScale,
                   WithAlpha(skin.InputPlaceholder, effectiveOpacity), TextLayer(*this, skin));
        clip(mark);
    }

    // Cursor — 2px wide, blinks at 0.5s on / 0.5s off
    if (_focused && !HasSelection() && _cursorBlink < 0.5f) {
        const f32 cx = myRect.X + pad + MeasureTextWidth(*skin.Font, Text.substr(0, _cursor), skin.FontScale) -
                _scrollX;
        const UIRect cur  = {cx - 1.0f, myRect.Y + 3.0f, 2.0f, myRect.H - 6.0f};
        const size_t mark = packet.UISprites.size();
        PushRect(packet, cur, WithAlpha(skin.InputCursor, effectiveOpacity),
                 whiteTex, whiteSampler, FillLayer(*this, skin));
        clip(mark);
    }
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------

f32 Slider::NormalizedValue() const
{
    if (Max <= Min) return 0.0f;
    return (Value - Min) / (Max - Min);
}

UIRect Slider::TrackRect(const UIRect myRect) const
{
    if (Style == Look::Bar) return myRect;
    return { myRect.X + _thumbHalf, myRect.Y + myRect.H * 0.5f - 3.0f, myRect.W - _thumbHalf * 2.0f, 6.0f };
}

UIRect Slider::ThumbRect(const UIRect track) const
{
    const f32 t    = NormalizedValue();
    const f32 cx   = track.X + t * track.W;
    const f32 half = _thumbHalf;
    return {cx - half, track.Y - 2.0f, half * 2.0f, track.H + 4.0f};
}

void Slider::SetFromMouseX(const f32 mouseX, const UIRect track)
{
    const f32 prevValue = Value;
    f32 t   = (mouseX - track.X) / track.W;
    t       = std::clamp(t, 0.0f, 1.0f);
    f32 raw = Min + t * (Max - Min);
    if (Step > 0.0f) raw = std::round(raw / Step) * Step;
    Value = std::clamp(raw, Min, Max);
    if (Value != prevValue) PlaySound(_interactSound);
    if (OnChanged) OnChanged(Value);
}

void Slider::OnUpdate(const f32 /*dt*/, const UIRect myRect, const Vec2    mousePos,
                      const bool                     mouseDown, const bool mouseJustDown,
                      const bool                     mouseJustUp, bool&    consumed)
{
    if (!Enabled || consumed) return;
    // Anywhere on the slider: a press jumps there and drags from it.
    const UIRect track = TrackRect(myRect);
    _hovered           = myRect.Contains(mousePos);

    if (mouseJustDown && _hovered) {
        _dragging = true;
        consumed  = true;
    }
    if (mouseJustUp) _dragging = false;
    if (_dragging && mouseDown) {
        SetFromMouseX(mousePos.X, track);
        consumed = true;
    }
}

void Slider::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin&       skin)
{
    _interactSound = skin.SoundSliderTick;
    if (Style == Look::Bar) return CollectBar(packet, myRect, effectiveOpacity, whiteTex, whiteSampler, skin);
    const UIRect track = TrackRect(myRect);
    PushRect(packet, track, WithAlpha(skin.ProgressBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    // Fill from left edge to thumb
    const UIRect fill = {track.X, track.Y, NormalizedValue() * track.W, track.H};
    PushRect(packet, fill, WithAlpha(skin.AccentColor, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));

    const UIRect thumb      = ThumbRect(track);
    const Color  thumbColor = (_hovered || _dragging || _focused)
                              ? skin.SliderThumbHovered
                              : skin.SliderThumb;
    PushRect(packet, thumb, WithAlpha(thumbColor, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}

// The bar look: the track and fill of a ProgressBar, and a handle at the
// fill's edge that brightens while hovered, dragged or focused.
void Slider::CollectBar(FramePacket& packet, const UIRect myRect, const f32 opacity, const TextureHandle whiteTex,
                        const SamplerHandle whiteSampler, const UISkin& skin) const
{
    PushRect(packet, myRect, WithAlpha(skin.ProgressBg, opacity), whiteTex, whiteSampler, BgLayer(*this, skin));
    const f32 fillW = NormalizedValue() * myRect.W;
    PushRect(packet, { myRect.X, myRect.Y, fillW, myRect.H }, WithAlpha(skin.ProgressFill, opacity), whiteTex, whiteSampler,
             FillLayer(*this, skin));

    constexpr f32 HandleW = 4.0f;
    const bool    active  = _hovered || _dragging || _focused;
    const f32     x       = std::clamp(myRect.X + fillW - HandleW * 0.5f, myRect.X, myRect.X + myRect.W - HandleW);
    PushRect(packet, { x, myRect.Y, HandleW, myRect.H }, WithAlpha(active ? skin.SliderThumbHovered : skin.SliderThumb, opacity),
             whiteTex, whiteSampler, TextLayer(*this, skin));
}

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

namespace
{
// ASCII letters fold to lower case; other bytes compare as they are, so
// "fran" finds "Français" and an exact kana string finds itself.
std::string Folded(const std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
} // namespace

void Dropdown::Open()
{
    _open = true;
    _filter.clear();
    Refilter();
    const auto selected = std::ranges::find(_shown, SelectedIndex);
    _highlight          = selected != _shown.end() ? static_cast<i32>(selected - _shown.begin()) : (_shown.empty() ? -1 : 0);
    ScrollToHighlight();
}

void Dropdown::Close()
{
    _open = false;
    _filter.clear();
}

// The items the filter matches - all of them with no filter - in order.
void Dropdown::Refilter()
{
    _shown.clear();
    const std::string filter = Folded(_filter);
    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const auto  index = static_cast<usize>(i);
        const bool  term  = index < SearchTerms.size() && Folded(SearchTerms[index]).contains(filter);
        if (filter.empty() || term || Folded(Items[index]).contains(filter)) _shown.push_back(i);
    }
    _highlight = _shown.empty() ? -1 : std::clamp(_highlight, 0, static_cast<i32>(_shown.size()) - 1);
    _scroll    = 0;
    ScrollToHighlight();
}

void Dropdown::Choose(const i32 shownIndex)
{
    if (shownIndex < 0 || shownIndex >= static_cast<i32>(_shown.size())) return;
    SelectedIndex = _shown[static_cast<usize>(shownIndex)];
    Close(); // before the callback, which may rebuild the screen
    if (OnChanged) OnChanged(SelectedIndex);
}

void Dropdown::ScrollToHighlight()
{
    if (_highlight < 0) return;
    const auto row = static_cast<u32>(_highlight);
    if (row < _scroll) _scroll = row;
    else if (row >= _scroll + MaxVisibleItems) _scroll = row - MaxVisibleItems + 1;
}

void Dropdown::OnNavStep(const i32 step)
{
    if (_shown.empty()) return;
    _highlight = std::clamp(_highlight + step, 0, static_cast<i32>(_shown.size()) - 1);
    ScrollToHighlight();
}

bool Dropdown::OnBackKey()
{
    if (!_open) return false;
    Close();
    return true;
}

void Dropdown::OnTextInput(const std::string_view chars)
{
    if (!_open || !Filters()) return;
    _filter += chars;
    Refilter();
}

void Dropdown::OnControlKey(const UIControlKey key)
{
    if (!_open || !Filters() || key != UIControlKey::Backspace || _filter.empty()) return;
    _filter.erase(Utf8::Prev(_filter, _filter.size()));
    Refilter();
}

// Confirm opens the list, then picks the highlighted item.
void Dropdown::OnActivate()
{
    if (!_open) Open();
    else if (_highlight >= 0) Choose(_highlight);
    else Close();
}

void Dropdown::OnFocusLost() { Close(); }

// The item rows of the open list: below the header, and below the filter row
// when there is one.
UIRect Dropdown::ListRect(const UIRect header) const
{
    const u32 rows = std::min(static_cast<u32>(std::max<usize>(_shown.size(), 1)), MaxVisibleItems);
    return { header.X, header.Y + header.H + (Filters() ? ItemHeight : 0.0f), header.W, ItemHeight * static_cast<f32>(rows) };
}

void Dropdown::UpdateTree(const f32 /*dt*/, const UIRect parent, const Vec2 mousePos, const bool /*mouseDown*/,
                          const bool mouseJustDown, const bool /*mouseJustUp*/, bool& consumed, const f32 scrollDelta)
{
    const UIRect myRect = ComputeRect(parent);
    if (!Visible || !Enabled) return;
    _headerHovered = myRect.Contains(mousePos);
    if (!_open) {
        if (mouseJustDown && _headerHovered) Open();
        return;
    }
    consumed = true; // open: nothing behind the list sees the mouse

    const UIRect list    = ListRect(myRect);
    const bool   inList  = list.Contains(mousePos);
    const i32    rowHere = inList ? static_cast<i32>(_scroll) + static_cast<i32>((mousePos.Y - list.Y) / ItemHeight) : -1;
    const bool   moved   = mousePos.X != _lastMouse.X || mousePos.Y != _lastMouse.Y;
    _lastMouse           = mousePos;
    if (inList && moved && rowHere < static_cast<i32>(_shown.size())) _highlight = rowHere; // a still mouse leaves the keys be

    if (inList && scrollDelta != 0.0f && _shown.size() > MaxVisibleItems) {
        const i32 most = static_cast<i32>(_shown.size() - MaxVisibleItems);
        _scroll        = static_cast<u32>(std::clamp(static_cast<i32>(_scroll) - static_cast<i32>(std::round(scrollDelta)), 0, most));
    }
    if (!mouseJustDown) return;
    const UIRect filterRow{ myRect.X, myRect.Y + myRect.H, myRect.W, Filters() ? ItemHeight : 0.0f };
    if (inList) Choose(rowHere);
    else if (!filterRow.Contains(mousePos)) Close(); // the header, or anywhere else
}

void Dropdown::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                         const TextureHandle whiteTex, const SamplerHandle whiteSampler, const UISkin& skin)
{
    const Color bg = _headerHovered || _focused ? skin.ButtonHovered : skin.ButtonNormal;
    PushRect(packet, myRect, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    if (!skin.Font) return;
    const f32         lineH   = skin.Font->GetLineHeight() * skin.FontScale;
    const f32         textY   = myRect.Y + (myRect.H - lineH) * 0.5f;
    const bool        valid   = SelectedIndex >= 0 && SelectedIndex < static_cast<i32>(Items.size());
    const std::string display = valid ? Items[static_cast<usize>(SelectedIndex)] : "\xE2\x80\x94"; // an em dash
    DrawTextUI(packet, *skin.Font, display, { myRect.X + 6.0f, textY }, skin.FontScale,
               WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));

    const std::string_view arrow = _open ? "\xE2\x96\xB2" : "\xE2\x96\xBC"; // up / down triangle
    DrawTextUI(packet, *skin.Font, arrow, { myRect.X + myRect.W - 20.0f, textY }, skin.FontScale,
               WithAlpha(skin.TextDisabled, effectiveOpacity), TextLayer(*this, skin));
}

// The open list draws above its siblings: the filter row (when it filters),
// then a window of MaxVisibleItems rows, with arrows where it scrolls.
void Dropdown::CollectList(FramePacket& packet, const UIRect header, const f32 opacity, const TextureHandle whiteTex,
                           const SamplerHandle whiteSampler, const UISkin& skin) const
{
    const i32  listZOrder = ZOrder + 500;
    const i32  bgL        = UIBaseLayer + (skin.ScreenLayerBase + listZOrder) * UILayerStride + UILayerBg;
    const i32  txtL       = UIBaseLayer + (skin.ScreenLayerBase + listZOrder) * UILayerStride + UILayerText;
    const f32  lh         = skin.Font ? skin.Font->GetLineHeight() * skin.FontScale : 0.0f;
    const auto text       = [&](const std::string_view s, const UIRect row, const Color c, const f32 x = 6.0f) {
        if (skin.Font) DrawTextUI(packet, *skin.Font, s, { row.X + x, row.Y + (row.H - lh) * 0.5f }, skin.FontScale, WithAlpha(c, opacity), txtL);
    };

    if (Filters()) {
        const UIRect row{ header.X, header.Y + header.H, header.W, ItemHeight };
        PushRect(packet, row, WithAlpha(skin.InputBg, opacity), whiteTex, whiteSampler, bgL);
        if (_filter.empty()) text(Loc::Get("ui.common.filter"), row, skin.InputPlaceholder);
        else text(_filter + "|", row, skin.TextNormal);
    }

    const UIRect list = ListRect(header);
    if (_shown.empty()) {
        PushRect(packet, list, WithAlpha(skin.PanelBg, opacity), whiteTex, whiteSampler, bgL);
        text(Loc::Get("ui.common.no_matches"), list, skin.TextDisabled);
        return;
    }
    const u32 end = std::min(static_cast<u32>(_shown.size()), _scroll + MaxVisibleItems);
    for (u32 r = _scroll; r < end; ++r) {
        const i32    item = _shown[r];
        const UIRect row{ list.X, list.Y + ItemHeight * static_cast<f32>(r - _scroll), list.W, ItemHeight };
        const Color  bg   = static_cast<i32>(r) == _highlight ? skin.ButtonHovered
                          : item == SelectedIndex            ? skin.ButtonPressed
                                                             : skin.PanelBg;
        PushRect(packet, row, WithAlpha(bg, opacity), whiteTex, whiteSampler, bgL);
        text(Items[static_cast<usize>(item)], row, skin.TextNormal);
        if (r == _scroll && _scroll > 0) text("\xE2\x96\xB2", row, skin.TextDisabled, row.W - 20.0f);         // more above
        if (r + 1 == end && end < _shown.size()) text("\xE2\x96\xBC", row, skin.TextDisabled, row.W - 20.0f); // more below
    }
}

void Dropdown::CollectTree(FramePacket& packet, const UIRect parent, const f32 parentOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler, const UISkin& skin)
{
    if (!Visible) return;
    const UIRect myRect = ComputeRect(parent);
    const f32    eff    = parentOpacity * Opacity;
    OnCollect(packet, myRect, eff, whiteTex, whiteSampler, skin);
    if (_open) CollectList(packet, myRect, eff, whiteTex, whiteSampler, skin);
    for (auto& child : _children)
        child->CollectTree(packet, myRect, eff, whiteTex, whiteSampler, skin);
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

void Checkbox::OnActivate()
{
    PlaySound(_interactSound);
    Checked = !Checked;
    if (OnChanged) OnChanged(Checked);
}

void Checkbox::OnUpdate(const f32 /*dt*/, const UIRect       myRect, const Vec2 mousePos,
                        const bool /*mouseDown*/, const bool mouseJustDown,
                        const bool /*mouseJustUp*/, bool&    consumed)
{
    if (!Enabled || consumed) return;
    const bool over = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
            mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _hovered = over;
    if (over && mouseJustDown) {
        OnActivate();
        consumed = true;
    }
}

void Checkbox::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                         const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                         const UISkin&       skin)
{
    _interactSound = skin.SoundToggle;
    const f32    boxSize = std::min(myRect.H, 18.0f);
    const UIRect box     = {myRect.X, myRect.Y + (myRect.H - boxSize) * 0.5f, boxSize, boxSize};
    const Color  bg      = _hovered ? skin.CheckboxHovered : skin.CheckboxBg;
    PushRect(packet, box, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    if (Checked) {
        const UIRect inner = {box.X + 3.0f, box.Y + 3.0f, box.W - 6.0f, box.H - 6.0f};
        PushRect(packet, inner, WithAlpha(skin.CheckboxCheck, effectiveOpacity),
                 whiteTex, whiteSampler, FillLayer(*this, skin));
    }

    if (!Label.empty() && skin.Font) {
        const f32 lineH = skin.Font->GetLineHeight() * skin.FontScale;
        const f32 textY = myRect.Y + (myRect.H - lineH) * 0.5f;
        DrawTextUI(packet, *skin.Font, Label,
                   {box.X + boxSize + 6.0f, textY},
                   skin.FontScale, WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));
    }
}

// ---------------------------------------------------------------------------
// RadioGroup
// ---------------------------------------------------------------------------

void RadioGroup::OnUpdate(const f32 /*dt*/, const UIRect       myRect, const Vec2 mousePos,
                          const bool /*mouseDown*/, const bool mouseJustDown,
                          const bool /*mouseJustUp*/, bool&    consumed)
{
    if (!Enabled || consumed) return;
    _hoveredIndex = -1;
    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const UIRect row  = {myRect.X, myRect.Y + ItemHeight * static_cast<f32>(i), myRect.W, ItemHeight};
        const bool   over = mousePos.X >= row.X && mousePos.X <= row.X + row.W &&
                mousePos.Y >= row.Y && mousePos.Y <= row.Y + row.H;
        if (over) _hoveredIndex = i;
        if (over && mouseJustDown) {
            SelectedIndex = i;
            if (OnChanged) OnChanged(SelectedIndex);
            consumed = true;
        }
    }
}

void RadioGroup::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin&       skin)
{
    const f32 dotR = 7.0f; // outer circle radius

    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const UIRect row = {myRect.X, myRect.Y + ItemHeight * static_cast<f32>(i), myRect.W, ItemHeight};
        const f32    cy  = row.Y + ItemHeight * 0.5f;
        const UIRect dot = {row.X, cy - dotR, dotR * 2.0f, dotR * 2.0f};
        const Color  bg  = (i == _hoveredIndex) ? skin.CheckboxHovered : skin.CheckboxBg;
        PushRect(packet, dot, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

        if (i == SelectedIndex) {
            const f32    innerR = dotR - 3.0f;
            const UIRect inner  = {dot.X + dotR - innerR, dot.Y + dotR - innerR, innerR * 2.0f, innerR * 2.0f};
            PushRect(packet, inner, WithAlpha(skin.CheckboxCheck, effectiveOpacity),
                     whiteTex, whiteSampler, FillLayer(*this, skin));
        }

        if (skin.Font) {
            const f32 lineH = skin.Font->GetLineHeight() * skin.FontScale;
            const f32 textY = row.Y + (row.H - lineH) * 0.5f;
            DrawTextUI(packet, *skin.Font, Items[static_cast<size_t>(i)],
                       {row.X + dotR * 2.0f + 6.0f, textY},
                       skin.FontScale, WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));
        }
    }
}

// ---------------------------------------------------------------------------
// Switch
// ---------------------------------------------------------------------------

void Switch::OnActivate()
{
    PlaySound(_interactSound);
    On = !On;
    if (OnChanged) OnChanged(On);
}

void Switch::OnUpdate(const f32                            dt, const UIRect myRect, const Vec2 mousePos,
                      const bool /*mouseDown*/, const bool mouseJustDown,
                      const bool /*mouseJustUp*/, bool&    consumed)
{
    if (!Enabled) return;
    const bool over = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
            mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _hovered = over;
    if (over && mouseJustDown && !consumed) {
        OnActivate();
        consumed = true;
    }

    // Animate thumb toward target
    const f32 target = On ? 1.0f : 0.0f;
    const f32 delta  = target - _animT;
    _animT           += std::copysign(std::min(std::abs(delta), AnimSpeed * dt), delta);
}

void Switch::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin&       skin)
{
    _interactSound = skin.SoundToggle;
    // Lerp track color between off and on
    const Color trackOff = skin.SwitchOff;
    const Color trackOn  = skin.SwitchOn;
    Color       track;
    track.R = trackOff.R + (trackOn.R - trackOff.R) * _animT;
    track.G = trackOff.G + (trackOn.G - trackOff.G) * _animT;
    track.B = trackOff.B + (trackOn.B - trackOff.B) * _animT;
    track.A = 1.0f;
    PushRect(packet, myRect, WithAlpha(track, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    const f32    pad    = 2.0f;
    const f32    thumbW = myRect.H - pad * 2.0f;
    const f32    travel = myRect.W - thumbW - pad * 2.0f;
    const UIRect thumb  = {myRect.X + pad + _animT * travel, myRect.Y + pad, thumbW, thumbW};
    PushRect(packet, thumb, WithAlpha(skin.SwitchThumb, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}
} // namespace Arcbit
