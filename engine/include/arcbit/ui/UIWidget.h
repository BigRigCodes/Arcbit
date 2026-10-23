#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/ui/UIDrag.h>
#include <arcbit/ui/UIRect.h>

#include <arcbit/ui/UISkin.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
struct FramePacket;

// Control keys forwarded to focused interactive widgets (e.g. TextInput).
// Shift variants extend the selection; Ctrl variants jump word boundaries.
enum class UIControlKey : u8
{
    Left,  ShiftLeft,  CtrlLeft,  CtrlShiftLeft,
    Right, ShiftRight, CtrlRight, CtrlShiftRight,
    Home,  ShiftHome,
    End,   ShiftEnd,
    Backspace, Delete, Enter, Escape,
    SelectAll, Copy, Cut, Paste, // Ctrl+A / C / X / V
};

// Base layer offset for all UI quads.  Each widget ZOrder level uses 4
// sublayers: 0=background, 1=fill/foreground, 2=text, 3=reserved.
static constexpr i32 UIBaseLayer   = 10'000'000;
static constexpr i32 UILayerStride = 4;
static constexpr i32 UILayerBg     = 0;
static constexpr i32 UILayerFill   = 1;
static constexpr i32 UILayerText   = 2;

// ---------------------------------------------------------------------------
// UIWidget — base class for all retained-mode UI widgets.
//
// Position is expressed as an anchor point on the parent rect plus a pixel
// offset, allowing layouts to adapt to different window sizes automatically.
//
// Children are owned by the parent and rendered in ascending ZOrder.
// ---------------------------------------------------------------------------
class UIWidget
{
public:
    // --- Layout fields -------------------------------------------------------

    // Normalized (0–1) point on the parent rect that this widget aligns to.
    // {0,0} = top-left, {0.5,0.5} = center, {1,1} = bottom-right.
    Vec2 Anchor = {0.0f, 0.0f};

    // Normalized point on this widget that aligns to the anchor.
    // {0,0} = top-left corner of the widget aligns to the anchor (default).
    // {0.5,0.5} = center of the widget aligns to the anchor.
    Vec2 Pivot = {0.0f, 0.0f};

    // Pixel offset applied after anchor/pivot alignment.
    Vec2 Offset = {0.0f, 0.0f};

    // Widget size in pixels.  Ignored per-axis when SizePercent > 0.
    Vec2 Size = {100.0f, 100.0f};

    // Overrides Size per axis as a fraction of the parent's size when > 0.
    // e.g. {1.0, 0} = full parent width, fixed height from Size.Y.
    Vec2 SizePercent = {0.0f, 0.0f};

    // --- Visual / interaction fields ----------------------------------------

    // Opacity multiplier applied to this widget and all its children.
    f32 Opacity = 1.0f;

    bool Visible = true; // false = not drawn and not hit-tested
    bool Enabled = true; // false = drawn at reduced opacity but not interactive

    // Sort key within the same screen layer.  Higher values render on top.
    i32 ZOrder = 0;

    // Whether this widget participates in keyboard/gamepad focus navigation.
    bool Focusable = false;
    
    // Sort order for keyboard/gamepad & tab focus navigation. Lower values are focused first.
    u32 TabOrder = 0;

    // Optional identifier used by UIScreen::FindWidget to locate this widget.
    std::string Name;

    // Per-widget skin overrides. Set any field to override that skin property
    // for this widget's OnCollect call. See UISkinOverride in UISkin.h.
    UISkinOverride SkinOverride;

    // --- Tooltip (docs/gui.md#tooltips) -------------------------------------

    // Shown after the pointer rests on the widget (UISkin::TooltipDelay), or
    // once focus moves to it by keys. Rich text. TooltipKey is a Loc key, read
    // when shown; GetTooltip, when set, is asked instead (text that changes).
    std::string                  Tooltip;
    std::string                  TooltipKey;
    std::function<std::string()> GetTooltip;

    // The tooltip as it would show now; empty: none.
    [[nodiscard]] std::string TooltipText() const;

    // --- Drag and drop (docs/gui.md#drag-and-drop; UIDrag.h) ----------------

    // A source: dragging from it asks what's dragged - nullopt, nothing (an
    // empty slot). OnDragEnd follows when the drag ends: dropped on a target
    // that took it, or not (dropped nowhere, refused, cancelled with Back).
    std::function<std::optional<DragPayload>(const DragStart&)> OnDragStart;
    std::function<void(const DragPayload&, bool dropped)>       OnDragEnd;

    // A target: whether it takes a payload (unset: anything) - drawn with
    // the skin's DropAccept / DropRefuse outline while one is over it - and
    // taking it.
    std::function<bool(const DragPayload&)> CanDrop;
    std::function<void(const DragPayload&)> OnDrop;

    [[nodiscard]] bool IsDragSource() const { return static_cast<bool>(OnDragStart); }
    [[nodiscard]] bool IsDropTarget() const { return static_cast<bool>(OnDrop); }
    [[nodiscard]] bool Accepts(const DragPayload& payload) const { return IsDropTarget() && (!CanDrop || CanDrop(payload)); }

    // While a drag is on: this widget is its source / the target it's over.
    [[nodiscard]] bool IsBeingDragged() const { return _dragSource; }
    [[nodiscard]] bool IsDragOver() const { return _dragOver; }

    virtual ~UIWidget() { ++s_destroyedCount; }

    // How many widgets have ever been destroyed - lets a screen notice, for
    // free, when its focused widget might be gone (UIScreen::GetFocusedWidget).
    [[nodiscard]] static u64 DestroyedCount() { return s_destroyedCount; }

    // Text and control-key events forwarded by UIManager to the focused widget.
    virtual void OnTextInput(std::string_view /*chars*/) {}
    virtual void OnControlKey(UIControlKey /*key*/) {}

    // When true UIManager suppresses Left/Right focus navigation, allowing the
    // widget to handle cursor movement directly. TextInput sets this when focused.
    [[nodiscard]] virtual bool ConsumesFocusNav() const { return false; }

    // While ConsumesFocusNav: the arrows / D-pad step (+1 down, -1 up) that
    // would have moved focus (an open Dropdown moves its highlight).
    virtual void OnNavStep(i32 /*step*/) {}

    // While ConsumesFocusNav: Back was pressed. True if the widget dealt with
    // it (an open Dropdown closes); false lets the widget lose focus.
    virtual bool OnBackKey() { return false; }

    // While focused: whether typing should arrive as text (the OS's text
    // input on, the game's keys held back).
    [[nodiscard]] virtual bool WantsTextInput() const { return ConsumesFocusNav(); }

    // The rect this widget lays its children out in, given its own: itself
    // for most widgets, a ScrollPanel's scrolled content area. Everything that
    // places children - update, draw, click-to-focus - goes through it, so a
    // click finds what is actually drawn under it.
    [[nodiscard]] virtual UIRect ChildRect(const UIRect myRect) const { return myRect; }

    // True when children outside this widget's own rect are cut off (a
    // ScrollPanel): they can't be seen there, so they can't be clicked there.
    [[nodiscard]] virtual bool ClipsChildren() const { return false; }

    // Compute this widget's pixel rect relative to parentRect.
    [[nodiscard]] UIRect ComputeRect(UIRect parent) const;

    // True while this widget has keyboard / gamepad focus.
    [[nodiscard]] bool IsFocused() const { return _focused; }

    // Returns a copy of base with this widget's SkinOverride fields applied.
    // Called automatically by CollectTree before OnCollect — no need to call
    // this manually inside OnCollect implementations.
    [[nodiscard]] UISkin GetEffectiveSkin(const UISkin& base) const;

    // Add a child widget. Returns a raw pointer for convenience; ownership
    // stays with this widget.
    template <typename T, typename... Args>
    T* AddChild(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T*   ptr   = child.get();
        _children.push_back(std::move(child));
        return ptr;
    }

    // Add a pre-constructed widget (used by UILoader).
    UIWidget* AddChildRaw(std::unique_ptr<UIWidget> child);

    void ClearChildren() { _children.clear(); }

    // The language changed (Loc's generation moved): this widget and every
    // descendant re-read their text keys. UIScreen calls it.
    void LocaleChangedTree();

    // Search this widget and all descendants by Name. Returns nullptr if not found.
    [[nodiscard]] UIWidget* FindDescendant(std::string_view name);
    
    // Tree traversal — virtual so ScrollPanel and other containers can override
    // child iteration to apply scroll offsets and clip rect registration.
    virtual void UpdateTree(f32   dt, UIRect      parent, Vec2        mousePos,
                            bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                            bool& consumed, f32 scrollDelta = 0.0f);

    virtual void CollectTree(FramePacket&  packet, UIRect          parent, f32 parentOpacity,
                             TextureHandle whiteTex, SamplerHandle whiteSampler,
                             const UISkin& skin);

protected:
    // Called once per frame. Override to update interactive state.
    virtual void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                          bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                          bool& consumed) {}

    // Emit render quads into the packet for this widget only (not children).
    // whiteTex / whiteSampler: 1×1 white texture for solid-color quads.
    virtual void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                           TextureHandle whiteTex, SamplerHandle whiteSampler,
                           const UISkin& skin) = 0;

    // Re-read text from its key (Label::TextKey and the like).
    virtual void OnLocaleChanged() {}

    // The outline a drop target gets while a payload is over it.
    void CollectDropOutline(FramePacket& packet, UIRect myRect, f32 alpha, TextureHandle whiteTex,
                            SamplerHandle whiteSampler, const UISkin& skin) const;

    // Focus lifecycle hooks — called by UIScreen during focus navigation.
    virtual void OnFocusGained() {}
    virtual void OnFocusLost()   {}

    // Called by UIScreen when confirm input (Enter/gamepad-A) is pressed
    // while this widget holds focus.
    virtual void OnActivate() {}

    // True while this widget is the focused widget in its screen.
    // Read in OnCollect to apply a focus highlight (e.g. Button uses it).
    bool _focused = false;

    // Set by the screen while a drag is on (IsBeingDragged / IsDragOver):
    // CollectTree outlines a target, and dims the source.
    bool _dragSource  = false;
    bool _dragOver    = false;
    bool _dragAccepts = false;

    // Sound key cached from the effective skin during the last OnCollect pass.
    // Interactive widgets set this in their OnCollect; OnActivate and mouse-click
    // paths play it via AudioManager::PlayOneShot when the key is non-empty.
    std::string _interactSound;

    std::vector<std::unique_ptr<UIWidget>> _children;

private:
    friend class UIScreen;

    inline static u64 s_destroyedCount = 0;
    inline static u64 s_lastSerial     = 0;

    // Unique per widget ever made: a new widget can reuse a destroyed one's
    // address, but never its serial (UIScreen's focus check needs both).
    const u64 _serial = ++s_lastSerial;
};
} // namespace Arcbit
