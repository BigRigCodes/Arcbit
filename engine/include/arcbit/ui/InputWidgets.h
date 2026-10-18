#pragma once

#include <arcbit/ui/UIWidget.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// TextInput — single-line editable text field.
//
// Modes:
//   Text    — any printable ASCII character is accepted.
//   Numeric — digits, one leading minus, one decimal point.
//   Regex   — each character is matched against Pattern before insertion.
//
// Wire OnConfirm to react when the user presses Enter.
// Wire OnChanged to react on every character change.
// ---------------------------------------------------------------------------
class TextInput : public UIWidget
{
public:
    enum class Mode : u8 { Text, Numeric, Regex };

    std::string Text;
    std::string Placeholder;
    std::string PlaceholderKey; // when set, Placeholder is Loc::Get(PlaceholderKey), again whenever the language changes
    std::string Pattern; // regex string; used only in Mode::Regex
    Mode        InputMode = Mode::Text;
    u32         MaxLength = 256; // characters (codepoints), not bytes

    std::function<void(const std::string&)> OnChanged;
    std::function<void(const std::string&)> OnConfirm;

    // What typing would most likely go on with ("lp" after "he"), drawn
    // dimmed after the text while the cursor is at its end. The owner fills
    // it in and decides what accepts it (the console: Tab).
    std::string Suggestion;

    TextInput() { Focusable = true; }

    // Replace the text and put the cursor at its end - for code changing the
    // text while it's being edited (a console line cleared after Enter, a
    // recalled history line). Writing Text directly works too: the cursor is
    // pulled back inside it before the next edit.
    void SetText(std::string text);

    [[nodiscard]] bool ConsumesFocusNav() const override { return _focused; }

    void OnTextInput(std::string_view chars) override;
    void OnControlKey(UIControlKey key) override;

protected:
    void OnLocaleChanged() override;
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnFocusGained() override { _cursorBlink = 0.0f; }

    void OnFocusLost() override
    {
        _scrollX   = 0.0f;
        _selAnchor = _cursor;
    }

    void OnActivate() override { if (OnConfirm) OnConfirm(Text); }

private:
    u32 _cursor      = 0; // insertion point (byte index into Text)
    u32 _selAnchor   = 0; // other end of selection; == _cursor when no selection
    f32 _scrollX     = 0.0f;
    f32 _cursorBlink = 0.0f;

    [[nodiscard]] bool HasSelection() const { return _selAnchor != _cursor; }
    void               ClampCursor(); // Text was changed from outside: keep the cursor inside it, on a character
    [[nodiscard]] u32  SelMin() const { return _cursor < _selAnchor ? _cursor : _selAnchor; }
    [[nodiscard]] u32  SelMax() const { return _cursor > _selAnchor ? _cursor : _selAnchor; }

    [[nodiscard]] bool AcceptsChar(u32 codepoint) const;
    void               DeleteSelection();
    void               MoveCursor(u32 pos, bool extendSel);
    [[nodiscard]] u32  PrevChar() const; // the byte offset one character before / after the cursor
    [[nodiscard]] u32  NextChar() const;
    [[nodiscard]] u32  PrevWordBoundary() const;
    [[nodiscard]] u32  NextWordBoundary() const;
    void               UpdateScroll(f32 availW, const UISkin& skin);
};

// ---------------------------------------------------------------------------
// Slider — horizontal drag slider mapping a float value to [Min, Max].
//
// Step == 0 means continuous; Step > 0 snaps value to multiples of Step.
// Wire OnChanged to react to every value change.
// ---------------------------------------------------------------------------
class Slider : public UIWidget
{
public:
    // Line: a thin track with a thumb. Bar: drawn like a ProgressBar (the
    // skin's ProgressBg / ProgressFill, full height) with a slim handle at
    // the fill's edge - a settings row's volume.
    enum class Look : u8 { Line, Bar };

    f32  Value = 0.5f;
    Look Style = Look::Line;
    f32 Min   = 0.0f;
    f32 Max   = 1.0f;
    f32 Step  = 0.0f; // 0 = continuous

    std::function<void(f32)> OnChanged;

    Slider() { Focusable = true; }

protected:
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

private:
    bool _dragging  = false;
    bool _hovered   = false;
    f32  _thumbHalf = 6.0f; // half-width of thumb in pixels

    [[nodiscard]] f32    NormalizedValue() const;
    [[nodiscard]] UIRect TrackRect(UIRect myRect) const; // where values lie: the bar, or the line between thumb halves
    [[nodiscard]] UIRect ThumbRect(UIRect track) const;
    void                 CollectBar(FramePacket& packet, UIRect myRect, f32 opacity, TextureHandle whiteTex,
                                    SamplerHandle whiteSampler, const UISkin& skin) const;
    void                 SetFromMouseX(f32 mouseX, UIRect track);
};

// ---------------------------------------------------------------------------
// Dropdown — click to open an item list; clicking an item selects it.
//
// The expanded list is rendered at ZOrder + 500 to appear above siblings.
// SelectedIndex == -1 means nothing is selected.
// ---------------------------------------------------------------------------
class Dropdown : public UIWidget
{
public:
    std::vector<std::string> Items;
    std::vector<std::string> ItemKeys;    // when set, Items are these keys' strings, again whenever the language changes
    std::vector<std::string> SearchTerms; // optional, one per item: more text the filter matches (a language's code)
    i32                      SelectedIndex = -1;

    // With more items than this, typing while the list is open filters it to
    // the items containing what's typed (or whose search terms do), ignoring
    // the case of ASCII letters.
    u32 FilterThreshold = 8;
    u32 MaxVisibleItems = 8;     // the open list scrolls past this many rows
    f32 ItemHeight      = 28.0f;

    std::function<void(i32)> OnChanged;

    Dropdown() { Focusable = true; }

    [[nodiscard]] bool                    IsOpen() const { return _open; }
    [[nodiscard]] bool                    Filters() const { return Items.size() > FilterThreshold; }
    [[nodiscard]] const std::string&      GetFilter() const { return _filter; }
    [[nodiscard]] const std::vector<i32>& GetShown() const { return _shown; } // the item indices the open list shows
    void                                  Open();
    void                                  Close();

    // Open, the list takes the arrows / D-pad, Back (closes it) and typing.
    [[nodiscard]] bool ConsumesFocusNav() const override { return _open; }
    [[nodiscard]] bool WantsTextInput() const override { return _open && Filters(); }
    void               OnNavStep(i32 step) override;
    bool               OnBackKey() override;
    void               OnTextInput(std::string_view chars) override;
    void               OnControlKey(UIControlKey key) override;

protected:
    void OnLocaleChanged() override;

    void UpdateTree(f32   dt, UIRect      parent, Vec2        mousePos,
                    bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                    bool& consumed, f32   scrollDelta = 0.0f) override;

    void CollectTree(FramePacket&  packet, UIRect          parent, f32 parentOpacity,
                     TextureHandle whiteTex, SamplerHandle whiteSampler,
                     const UISkin& skin) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnActivate() override; // opens; when open, picks the highlighted item
    void OnFocusLost() override;

private:
    bool             _open          = false;
    bool             _headerHovered = false;
    i32              _highlight     = -1; // index into _shown: the keys' (or the mouse's) row
    u32              _scroll        = 0;  // the first shown row drawn
    Vec2             _lastMouse     = { -1.0f, -1.0f };
    std::string      _filter;
    std::vector<i32> _shown;

    void   Refilter();
    void   Choose(i32 shownIndex);
    void   ScrollToHighlight();
    UIRect ListRect(UIRect header) const;
    void   CollectList(FramePacket&  packet, UIRect          headerRect, f32 opacity,
                       TextureHandle whiteTex, SamplerHandle whiteSampler,
                       const UISkin& skin) const;
};

// ---------------------------------------------------------------------------
// Checkbox — simple boolean toggle with a checkmark indicator.
// ---------------------------------------------------------------------------
class Checkbox : public UIWidget
{
public:
    bool        Checked = false;
    std::string Label;
    std::string LabelKey; // when set, Label is Loc::Get(LabelKey), again whenever the language changes

    std::function<void(bool)> OnChanged;

    Checkbox() { Focusable = true; }
    void SetLabelKey(std::string key);

protected:
    void OnLocaleChanged() override;
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnActivate() override;

private:
    bool _hovered = false;
};

// ---------------------------------------------------------------------------
// RadioGroup — a group of mutually exclusive options rendered vertically.
//
// SelectedIndex == -1 means nothing is selected initially.
// ItemHeight controls the per-row height; the widget height should match
// Items.size() * ItemHeight.
// ---------------------------------------------------------------------------
class RadioGroup : public UIWidget
{
public:
    std::vector<std::string> Items;
    std::vector<std::string> ItemKeys; // when set, Items are these keys' strings, again whenever the language changes
    i32                      SelectedIndex = -1;
    f32                      ItemHeight    = 28.0f;

    std::function<void(i32)> OnChanged;

    RadioGroup() { Focusable = true; }

protected:
    void OnLocaleChanged() override;
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

private:
    i32 _hoveredIndex = -1;
};

// ---------------------------------------------------------------------------
// Switch — binary toggle with an animated thumb that slides left/right.
//
// On = true → thumb slides to the right; Off = false → thumb slides left.
// AnimSpeed controls how fast the thumb moves (units: full-width per second).
// ---------------------------------------------------------------------------
class Switch : public UIWidget
{
public:
    bool On        = false;
    f32  AnimSpeed = 6.0f;

    std::function<void(bool)> OnChanged;

    Switch() { Focusable = true; }

protected:
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnActivate() override;

private:
    bool _hovered = false;
    f32  _animT   = 0.0f; // 0 = fully left (off), 1 = fully right (on)
};
} // namespace Arcbit
