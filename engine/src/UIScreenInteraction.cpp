// UIScreen: drag and drop and tooltips (docs/gui.md#drag-and-drop,
// docs/gui.md#tooltips). UIManager feeds the pointer in (Interact) and the
// grab / rotate / back keys; the screen draws the tooltip and the ghost above
// its widgets.

#include <arcbit/ui/UIScreen.h>

#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RichText.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Arcbit
{
namespace
{
    constexpr f32 DragThreshold = 6.0f;  // pixels the pointer moves, button held, before a press becomes a drag
    constexpr i32 TooltipZ      = 900;   // above the screen's widgets, under the next screen up
    constexpr i32 GhostZ        = 950;
    constexpr f32 TooltipPad    = 8.0f;

    i32 LayerAt(const UISkin& skin, const i32 z, const i32 sub) { return UIBaseLayer + (skin.ScreenLayerBase + z) * UILayerStride + sub; }

    void Quad(FramePacket& packet, const UIRect r, const Color c, const TextureHandle tex, const SamplerHandle sampler,
              const i32 layer, const UVRect uv = {}, const f32 rotation = 0.0f)
    {
        Sprite s{};
        s.Texture  = tex;
        s.Sampler  = sampler;
        s.Position = { r.X + r.W * 0.5f, r.Y + r.H * 0.5f };
        s.Size     = { r.W, r.H };
        s.UV       = uv;
        s.Tint     = c;
        s.Rotation = rotation;
        s.Layer    = layer;
        packet.UISprites.push_back(s);
    }

    f32 Distance(const Vec2 a, const Vec2 b) { return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y)); }
} // namespace

// ---------------------------------------------------------------------------
// Finding widgets
// ---------------------------------------------------------------------------

UIWidget* UIScreen::Resolve(const WidgetRef& ref) const
{
    if (!ref.Ptr) return nullptr;
    const bool inTree = std::ranges::any_of(_roots, [&](const auto& root) { return InTree(*root, ref.Ptr); });
    return inTree && ref.Ptr->_serial == ref.Serial ? ref.Ptr : nullptr; // in the tree: alive, safe to read
}

// The deepest visible, enabled widget under pos that `wanted` picks; the
// topmost first, the way FindFocusableHit searches.
UIWidget* UIScreen::FindHitIn(UIWidget& w, const UIRect parent, const Vec2 pos, const std::function<bool(const UIWidget&)>& wanted)
{
    if (!w.Visible || !w.Enabled) return nullptr;
    const UIRect myRect = w.ComputeRect(parent);
    if (!w.ClipsChildren() || myRect.Contains(pos)) {
        const UIRect inner = w.ChildRect(myRect);
        for (auto it = w._children.rbegin(); it != w._children.rend(); ++it)
            if (UIWidget* hit = FindHitIn(**it, inner, pos, wanted)) return hit;
    }
    return wanted(w) && myRect.Contains(pos) ? &w : nullptr;
}

UIWidget* UIScreen::FindHit(const UIRect screenRect, const Vec2 pos, const std::function<bool(const UIWidget&)>& wanted) const
{
    for (auto it = _roots.rbegin(); it != _roots.rend(); ++it)
        if (UIWidget* hit = FindHitIn(**it, screenRect, pos, wanted)) return hit;
    return nullptr;
}

// Where a widget is on screen, found by walking down to it.
std::optional<UIRect> UIScreen::RectOf(const UIWidget* widget, const UIRect screenRect) const
{
    std::function<std::optional<UIRect>(const UIWidget&, UIRect)> find = [&](const UIWidget& w, const UIRect parent) -> std::optional<UIRect> {
        const UIRect myRect = w.ComputeRect(parent);
        if (&w == widget) return myRect;
        for (const auto& child : w._children)
            if (auto found = find(*child, w.ChildRect(myRect))) return found;
        return std::nullopt;
    };
    for (const auto& root : _roots)
        if (auto found = find(*root, screenRect)) return found;
    return std::nullopt;
}

GridPanel* UIScreen::FindGridOf(const UIWidget* widget) const
{
    std::function<GridPanel*(UIWidget&)> find = [&](UIWidget& w) -> GridPanel* {
        for (const auto& child : w._children) {
            if (child.get() == widget) return dynamic_cast<GridPanel*>(&w);
            if (GridPanel* found = find(*child)) return found;
        }
        return nullptr;
    };
    for (const auto& root : _roots)
        if (GridPanel* found = find(*root)) return found;
    return nullptr;
}

std::vector<UIWidget*> UIScreen::AddFromTemplate(const std::string_view name, UIWidget& parent, const u32 count)
{
    std::vector<UIWidget*> made;
    for (u32 i = 0; i < count; ++i)
        if (UIWidget* w = AddFromTemplate(name, parent)) made.push_back(w);
    return made;
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

UIPointer UIScreen::Interact(const f32 dt, const UIRect screenRect, const UIPointer& pointer, const UISkin& skin)
{
    _pointer    = pointer;
    _screenRect = screenRect;
    if (_drag.Active && !_drag.Keyboard) CarryDrag(pointer, screenRect);
    else if (!_drag.Active) BeginPointerDrag(pointer, screenRect);
    UpdateTooltip(dt, pointer, screenRect, skin);
    if (!_drag.Active) return pointer;
    UIPointer quiet = pointer; // nothing under a drag clicks
    quiet.Down = quiet.JustDown = quiet.JustUp = quiet.AltDown = quiet.AltJustDown = quiet.AltJustUp = false;
    return quiet;
}

// A press on a drag source becomes a drag once the pointer moves far enough
// with the button held.
void UIScreen::BeginPointerDrag(const UIPointer& p, const UIRect screenRect)
{
    if (p.JustDown || p.AltJustDown) {
        _drag.Pressed  = Ref(FindHit(screenRect, p.Pos, [](const UIWidget& w) { return w.IsDragSource(); }));
        _drag.PressPos = p.Pos;
        _drag.PressAlt = p.AltJustDown && !p.JustDown;
    }
    const bool held = _drag.PressAlt ? p.AltDown : p.Down;
    UIWidget*  from = Resolve(_drag.Pressed);
    if (!from || !held) {
        _drag.Pressed = {};
        return;
    }
    if (Distance(p.Pos, _drag.PressPos) < DragThreshold) return;
    _drag.Pressed = {};
    std::optional<DragPayload> payload = from->OnDragStart(DragStart{ .Keyboard = false, .Alternate = _drag.PressAlt });
    if (!payload) return;
    _drag.Active   = true;
    _drag.Keyboard = false;
    _drag.Alt      = _drag.PressAlt;
    _drag.Source   = Ref(from);
    _drag.Payload  = std::move(*payload);
    from->_dragSource = true;
    HideTooltip();
    CarryDrag(p, screenRect);
}

// The target follows the pointer; letting go of the button drops.
void UIScreen::CarryDrag(const UIPointer& p, const UIRect screenRect)
{
    SetDragTarget(FindHit(screenRect, p.Pos, [](const UIWidget& w) { return w.IsDropTarget(); }));
    if (_drag.Alt ? p.AltJustUp || !p.AltDown : p.JustUp || !p.Down) EndDrag(true);
}

void UIScreen::SetDragTarget(UIWidget* target)
{
    if (UIWidget* old = Resolve(_drag.Target); old && old != target) old->_dragOver = old->_dragAccepts = false;
    _drag.Target = Ref(target);
    if (!target) return;
    target->_dragOver    = true;
    target->_dragAccepts = target->Accepts(_drag.Payload);
}

// Drop on the target if it takes the payload (drop: false, a cancel), then
// tell the source either way. The drag is over before anyone hears of it, so
// either may rebuild the screen.
void UIScreen::EndDrag(const bool drop)
{
    DragState ended = std::move(_drag);
    _drag           = {};
    UIWidget* source = Resolve(ended.Source);
    UIWidget* target = Resolve(ended.Target);
    if (source) source->_dragSource = false;
    if (target) target->_dragOver = target->_dragAccepts = false;
    _endingKeyboard    = ended.Keyboard; // the target may ask how it came (IsKeyboardDrag) while it takes it
    const bool dropped = drop && target && target->Accepts(ended.Payload);
    if (dropped) target->OnDrop(ended.Payload);
    _endingKeyboard = false;
    if (UIWidget* still = Resolve(ended.Source); still && still->OnDragEnd) still->OnDragEnd(ended.Payload, dropped);
}

void UIScreen::GrabFocused(const bool alternate)
{
    UIWidget* focused = GetFocusedWidget();
    if (_drag.Active) {
        if (!_drag.Keyboard || !focused) return;
        SetDragTarget(focused->IsDropTarget() ? focused : nullptr);
        if (focused->Accepts(_drag.Payload)) EndDrag(true); // somewhere that won't take it: keep holding
        return;
    }
    if (!focused || !focused->IsDragSource()) return;
    std::optional<DragPayload> payload = focused->OnDragStart(DragStart{ .Keyboard = true, .Alternate = alternate });
    if (!payload) return;
    _drag.Active      = true;
    _drag.Keyboard    = true;
    _drag.Source      = Ref(focused);
    _drag.Payload     = std::move(*payload);
    focused->_dragSource = true;
    HideTooltip();
}

void UIScreen::RotateDrag()
{
    if (_drag.Active && _drag.Payload.Rotatable) _drag.Payload.Rotated = !_drag.Payload.Rotated;
    if (UIWidget* target = Resolve(_drag.Target)) target->_dragAccepts = target->Accepts(_drag.Payload); // it may fit now
}

void UIScreen::CancelDrag()
{
    if (_drag.Active) EndDrag(false);
}

// ---------------------------------------------------------------------------
// Tooltips
// ---------------------------------------------------------------------------

void UIScreen::HideTooltip()
{
    _tooltip = {};
}

void UIScreen::FocusMovedByKeys()
{
    UIWidget* focused = GetFocusedWidget();
    _tooltip          = {};
    _tooltip.Focus    = true;
    _tooltip.Over     = Ref(focused && !focused->TooltipText().empty() ? focused : nullptr);
    if (_drag.Active && _drag.Keyboard) SetDragTarget(focused && focused->IsDropTarget() ? focused : nullptr);
}

void UIScreen::UpdateTooltip(const f32 dt, const UIPointer& p, const UIRect screenRect, const UISkin& skin)
{
    const bool pointerMoved = Distance(p.Pos, _tooltip.At) > 2.0f;
    if (_drag.Active || p.Down || p.AltDown) {
        HideTooltip();
        return;
    }
    if (_tooltip.Focus && pointerMoved && _tooltip.Shown) HideTooltip(); // the mouse is back
    if (!_tooltip.Focus) {
        UIWidget* over = FindHit(screenRect, p.Pos, [](const UIWidget& w) { return !w.TooltipText().empty(); });
        if (over != Resolve(_tooltip.Over)) {
            _tooltip      = {};
            _tooltip.Over = Ref(over);
        }
    }
    UIWidget* over = Resolve(_tooltip.Over);
    if (!over) {
        _tooltip.Shown = false;
        _tooltip.Text.clear();
        return;
    }
    _tooltip.Timer += dt;
    if (!_tooltip.Shown && _tooltip.Timer >= skin.TooltipDelay) {
        _tooltip.Shown = true;
        _tooltip.At    = p.Pos;
    }
    _tooltip.Text = _tooltip.Shown ? over->TooltipText() : std::string{};
}

void UIScreen::CollectTooltip(FramePacket& packet, const UISkin& skin, const TextureHandle whiteTex, const SamplerHandle whiteSampler) const
{
    if (_tooltip.Text.empty() || !skin.Font) return;
    const TextLayout layout = LayoutRichText(*skin.Font, _tooltip.Text,
                                             { .Scale = skin.FontScale, .WrapWidth = skin.TooltipWidth, .Style = skin.Text });
    const Vec2 size = { layout.Size.X + TooltipPad * 2.0f, layout.Size.Y + TooltipPad * 2.0f };
    Vec2       at   = { _tooltip.At.X + 16.0f, _tooltip.At.Y + 20.0f }; // below-right of the pointer
    if (_tooltip.Focus)
        if (const auto rect = RectOf(Resolve(_tooltip.Over), _screenRect)) // under the focused widget, else over it
            at = { rect->X, rect->Y + rect->H + size.Y + 4.0f > _screenRect.H ? rect->Y - size.Y - 4.0f : rect->Y + rect->H + 4.0f };
    at.X = std::clamp(at.X, 0.0f, std::max(0.0f, _screenRect.W - size.X));
    at.Y = std::clamp(at.Y, 0.0f, std::max(0.0f, _screenRect.H - size.Y));

    const UIRect box{ at.X, at.Y, size.X, size.Y };
    Quad(packet, { box.X - 1.0f, box.Y - 1.0f, box.W + 2.0f, box.H + 2.0f }, skin.TooltipBorder, whiteTex, whiteSampler,
         LayerAt(skin, TooltipZ, UILayerBg));
    Quad(packet, box, skin.TooltipBg, whiteTex, whiteSampler, LayerAt(skin, TooltipZ, UILayerFill));
    DrawRichText(packet, *skin.Font, layout,
                 { .Position = { box.X + TooltipPad, box.Y + TooltipPad }, .Color = skin.TextNormal,
                   .Layer = LayerAt(skin, TooltipZ, UILayerText), .Time = GetTextClock(), .Style = skin.Text,
                   .White = whiteTex, .WhiteSampler = whiteSampler });
}

// The ghost: under the pointer, or over the focused widget for a grab-key drag.
void UIScreen::CollectGhost(FramePacket& packet, const UISkin& skin, const TextureHandle whiteTex, const SamplerHandle whiteSampler) const
{
    if (!_drag.Active) return;
    const DragPayload& p      = _drag.Payload;
    const Vec2         held   = p.HeldSize();
    Vec2               center = _pointer.Pos;
    if (_drag.Keyboard)
        if (const auto rect = RectOf(GetFocusedWidget(), _screenRect))
            center = { rect->X + std::max(0.0f, (rect->W - held.X) * 0.5f) + held.X * 0.5f,
                       rect->Y + std::max(0.0f, (rect->H - held.Y) * 0.5f) + held.Y * 0.5f };
    const UIRect box{ center.X - p.Size.X * 0.5f, center.Y - p.Size.Y * 0.5f, p.Size.X, p.Size.Y };
    const bool   textured = p.Texture.IsValid();
    Color        tint     = p.Tint;
    tint.A *= 0.6f; // see-through: a target's own highlight (where it would land) shows under it
    Quad(packet, box, tint, textured ? p.Texture : whiteTex, textured && p.Sampler.IsValid() ? p.Sampler : whiteSampler,
         LayerAt(skin, GhostZ, UILayerFill), p.UV, p.Rotated ? std::numbers::pi_v<f32> * 0.5f : 0.0f);
    if (p.Text.empty() || !skin.Font) return;
    const TextLayout layout = LayoutRichText(*skin.Font, p.Text, { .Scale = skin.FontScale, .Style = skin.Text });
    DrawRichText(packet, *skin.Font, layout,
                 { .Position = { center.X + held.X * 0.5f - layout.Size.X - 2.0f, center.Y + held.Y * 0.5f - layout.Size.Y },
                   .Color = skin.TextNormal, .Layer = LayerAt(skin, GhostZ, UILayerText), .Style = skin.Text,
                   .White = whiteTex, .WhiteSampler = whiteSampler });
}
} // namespace Arcbit
