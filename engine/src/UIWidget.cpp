#include <arcbit/ui/UIWidget.h>
#include <arcbit/core/Loc.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>

namespace Arcbit
{
UIWidget* UIWidget::AddChildRaw(std::unique_ptr<UIWidget> child)
{
    UIWidget* ptr = child.get();
    _children.push_back(std::move(child));
    return ptr;
}

void UIWidget::LocaleChangedTree()
{
    OnLocaleChanged();
    for (const auto& child : _children) child->LocaleChangedTree();
}

std::string UIWidget::TooltipText() const
{
    if (GetTooltip) return GetTooltip();
    return TooltipKey.empty() ? Tooltip : Loc::Get(TooltipKey);
}

UIWidget* UIWidget::FindDescendant(const std::string_view name)
{
    if (Name == name) return this;
    for (auto& child : _children)
        if (auto* found = child->FindDescendant(name)) return found;
    return nullptr;
}

UIRect UIWidget::ComputeRect(const UIRect parent) const
{
    const f32 w = (SizePercent.X > 0.0f) ? parent.W * SizePercent.X : Size.X;
    const f32 h = (SizePercent.Y > 0.0f) ? parent.H * SizePercent.Y : Size.Y;

    const f32 anchorX = parent.X + parent.W * Anchor.X;
    const f32 anchorY = parent.Y + parent.H * Anchor.Y;

    return {
        anchorX - w * Pivot.X + Offset.X,
        anchorY - h * Pivot.Y + Offset.Y,
        w, h
    };
}

void UIWidget::UpdateTree(const f32  dt, const UIRect      parent, const Vec2        mousePos,
                          const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp,
                          bool&      consumed, const f32 scrollDelta)
{
    if (!Visible) return;

    const UIRect myRect = ComputeRect(parent);

    // Update children in reverse ZOrder so highest ZOrder gets first crack.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::sort(sorted.begin(), sorted.end(),
              [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    for (UIWidget* child : sorted)
        child->UpdateTree(dt, myRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed, scrollDelta);

    if (Enabled)
        OnUpdate(dt, myRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed);
}

// A drop target with a payload over it: a 2px outline, the skin's accept or
// refuse color, above its own drawing.
void UIWidget::CollectDropOutline(FramePacket& packet, const UIRect r, const f32 alpha, const TextureHandle whiteTex,
                                  const SamplerHandle whiteSampler, const UISkin& skin) const
{
    Color color = _dragAccepts ? skin.DropAccept : skin.DropRefuse;
    color.A *= alpha;
    const i32 layer = UIBaseLayer + (skin.ScreenLayerBase + ZOrder) * UILayerStride + UILayerText;
    constexpr f32 T = 2.0f;
    for (const UIRect edge : { UIRect{ r.X, r.Y, r.W, T }, UIRect{ r.X, r.Y + r.H - T, r.W, T },
                               UIRect{ r.X, r.Y, T, r.H }, UIRect{ r.X + r.W - T, r.Y, T, r.H } }) {
        Sprite s{};
        s.Texture  = whiteTex;
        s.Sampler  = whiteSampler;
        s.Position = { edge.X + edge.W * 0.5f, edge.Y + edge.H * 0.5f };
        s.Size     = { edge.W, edge.H };
        s.Tint     = color;
        s.Layer    = layer;
        packet.UISprites.push_back(s);
    }
}

UISkin UIWidget::GetEffectiveSkin(const UISkin& base) const
{
    // NOTE: When adding a field to UISkin / UISkinOverride, add a merge line here.
    const auto& o = SkinOverride;
    UISkin s = base;
    if (o.Font)               s.Font               = *o.Font;
    if (o.FontScale)          s.FontScale          = *o.FontScale;
    if (o.Text)               s.Text               = *o.Text;
    if (o.PanelBg)            s.PanelBg            = *o.PanelBg;
    if (o.PanelBorder)        s.PanelBorder        = *o.PanelBorder;
    if (o.ButtonNormal)       s.ButtonNormal       = *o.ButtonNormal;
    if (o.ButtonHovered)      s.ButtonHovered      = *o.ButtonHovered;
    if (o.ButtonPressed)      s.ButtonPressed      = *o.ButtonPressed;
    if (o.ButtonDisabled)     s.ButtonDisabled     = *o.ButtonDisabled;
    if (o.TextNormal)         s.TextNormal         = *o.TextNormal;
    if (o.TextDisabled)       s.TextDisabled       = *o.TextDisabled;
    if (o.TextLabel)          s.TextLabel          = *o.TextLabel;
    if (o.ProgressBg)         s.ProgressBg         = *o.ProgressBg;
    if (o.ProgressFill)       s.ProgressFill       = *o.ProgressFill;
    if (o.ScrollTrack)        s.ScrollTrack        = *o.ScrollTrack;
    if (o.ScrollThumb)        s.ScrollThumb        = *o.ScrollThumb;
    if (o.ScrollThumbHovered) s.ScrollThumbHovered = *o.ScrollThumbHovered;
    if (o.AccentColor)        s.AccentColor        = *o.AccentColor;
    if (o.ScrimColor)         s.ScrimColor         = *o.ScrimColor;
    if (o.InputBg)            s.InputBg            = *o.InputBg;
    if (o.InputBorder)        s.InputBorder        = *o.InputBorder;
    if (o.InputFocusBorder)   s.InputFocusBorder   = *o.InputFocusBorder;
    if (o.InputCursor)        s.InputCursor        = *o.InputCursor;
    if (o.InputPlaceholder)   s.InputPlaceholder   = *o.InputPlaceholder;
    if (o.SliderThumb)        s.SliderThumb        = *o.SliderThumb;
    if (o.SliderThumbHovered) s.SliderThumbHovered = *o.SliderThumbHovered;
    if (o.CheckboxBg)         s.CheckboxBg         = *o.CheckboxBg;
    if (o.CheckboxHovered)    s.CheckboxHovered    = *o.CheckboxHovered;
    if (o.CheckboxCheck)      s.CheckboxCheck      = *o.CheckboxCheck;
    if (o.SwitchOn)           s.SwitchOn           = *o.SwitchOn;
    if (o.SwitchOff)          s.SwitchOff          = *o.SwitchOff;
    if (o.SwitchThumb)        s.SwitchThumb        = *o.SwitchThumb;
    if (o.SoundActivate)      s.SoundActivate      = *o.SoundActivate;
    if (o.SoundSliderTick)    s.SoundSliderTick    = *o.SoundSliderTick;
    if (o.SoundToggle)        s.SoundToggle        = *o.SoundToggle;
    return s;
}

void UIWidget::CollectTree(FramePacket&        packet, const UIRect          parent, const f32 parentOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin&       skin)
{
    if (!Visible) return;

    const UIRect myRect         = ComputeRect(parent);
    const f32    effectiveAlpha = Opacity * parentOpacity * (_dragSource ? 0.45f : 1.0f); // what's dragged dims
    const UISkin effectiveSkin  = GetEffectiveSkin(skin);

    OnCollect(packet, myRect, effectiveAlpha, whiteTex, whiteSampler, effectiveSkin);
    if (_dragOver && DropOutline) CollectDropOutline(packet, myRect, effectiveAlpha, whiteTex, whiteSampler, effectiveSkin);

    // Children sorted by ascending ZOrder so higher values render on top.
    // Children receive the original skin — overrides are per-widget, not cascading.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder < b->ZOrder; });

    for (UIWidget* child : sorted)
        child->CollectTree(packet, myRect, effectiveAlpha, whiteTex, whiteSampler, skin);
}
} // namespace Arcbit
