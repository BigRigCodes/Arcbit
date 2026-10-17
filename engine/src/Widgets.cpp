#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/Font.h>

#include <algorithm>
#include <string>

namespace { void PlaySound(const std::string& key) { if (!key.empty()) Arcbit::AudioManager::PlayOneShot(key); } }

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Helpers
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

// Push a solid-color quad into UISprites.
static void PushRect(FramePacket&        packet, const UIRect&    r, const Color& c,
                     const TextureHandle tex, const SamplerHandle samp, const i32 layer)
{
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = {r.X + r.W * 0.5f, r.Y + r.H * 0.5f};
    s.Size     = {r.W, r.H};
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

// Apply effective opacity to a color.
static Color WithAlpha(Color c, const f32 alpha)
{
    c.A *= alpha;
    return c;
}

// Push a textured quad (arbitrary UV) into UISprites.
static void PushTexturedRect(FramePacket& packet, const UIRect&  r, const UVRect&         uv,
                             const Color& c, const TextureHandle tex, const SamplerHandle samp, const i32 layer)
{
    if (r.W <= 0.0f || r.H <= 0.0f) return;
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = {r.X + r.W * 0.5f, r.Y + r.H * 0.5f};
    s.Size     = {r.W, r.H};
    s.UV       = uv;
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

// Emit nine quads for a nine-slice sprite into UISprites.
static void PushNineSlice(FramePacket&        packet, const UIRect     rect, const Color tint,
                          const TextureHandle tex, const SamplerHandle samp, const i32   layer,
                          const f32           uvBL, const f32          uvBR, const f32   uvBT, const f32 uvBB,
                          const f32           pxL, const f32           pxR, const f32    pxT, const f32  pxB)
{
    const f32 pL  = std::min(pxL, rect.W * 0.5f);
    const f32 pR  = std::min(pxR, rect.W * 0.5f);
    const f32 pT  = std::min(pxT, rect.H * 0.5f);
    const f32 pB  = std::min(pxB, rect.H * 0.5f);
    const f32 cX  = rect.X + pL,          cY  = rect.Y + pT;
    const f32 cW  = rect.W - pL - pR,     cH  = rect.H - pT - pB;
    const f32 rX  = rect.X + rect.W - pR, bY  = rect.Y + rect.H - pB;
    const f32 uvL = uvBL,                 uvT = uvBT;
    const f32 uvR = 1.0f - uvBR,          uvB = 1.0f - uvBB;

    struct Slice
    {
        UIRect s;
        UVRect uv;
    };
    const Slice slices[9] = {
        {{rect.X, rect.Y, pL, pT}, {0, 0, uvL, uvT}},
        {{cX, rect.Y, cW, pT}, {uvL, 0, uvR, uvT}},
        {{rX, rect.Y, pR, pT}, {uvR, 0, 1, uvT}},
        {{rect.X, cY, pL, cH}, {0, uvT, uvL, uvB}},
        {{cX, cY, cW, cH}, {uvL, uvT, uvR, uvB}},
        {{rX, cY, pR, cH}, {uvR, uvT, 1, uvB}},
        {{rect.X, bY, pL, pB}, {0, uvB, uvL, 1}},
        {{cX, bY, cW, pB}, {uvL, uvB, uvR, 1}},
        {{rX, bY, pR, pB}, {uvR, uvB, 1, 1}},
    };
    for (const auto& [sr, uv] : slices)
        PushTexturedRect(packet, sr, uv, tint, tex, samp, layer);
}

// Word-wrap text to fit within maxWidth pixels, preserving explicit newlines.
static std::string WrapText(const FontAtlas& font, const std::string_view text,
                            const f32        maxWidth, const f32          scale)
{
    const GlyphInfo* spGlyph = font.GetGlyph(' ');
    const f32        spaceW  = spGlyph ? spGlyph->Advance * scale : 0.0f;

    std::string      result;
    std::string_view remaining = text;

    while (true) {
        const auto       nl   = remaining.find('\n');
        std::string_view para = remaining.substr(0, nl);
        if (!result.empty()) result += '\n';

        f32  lineWidth = 0.0f;
        bool lineStart = true;
        while (!para.empty()) {
            const auto       sp    = para.find(' ');
            std::string_view word  = para.substr(0, sp);
            const f32        wordW = MeasureTextWidth(font, word, scale);
            const f32        addW  = lineStart ? wordW : spaceW + wordW;

            if (!lineStart && lineWidth + addW > maxWidth) {
                result    += '\n';
                lineWidth = 0.0f;
                lineStart = true;
            }
            if (!lineStart) {
                result    += ' ';
                lineWidth += spaceW;
            }
            result    += std::string(word);
            lineWidth += wordW;
            lineStart = false;

            if (sp == std::string_view::npos) break;
            para = para.substr(sp + 1);
        }

        if (nl == std::string_view::npos) break;
        remaining = remaining.substr(nl + 1);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void Panel::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                      const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                      const UISkin&       skin)
{
    Collect(packet, myRect, effectiveOpacity, whiteTex, whiteSampler, skin, skin.PanelBg);
}

void Panel::Collect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity, const TextureHandle whiteTex,
                    const SamplerHandle whiteSampler, const UISkin& skin, const Color baseColor) const
{
    PushRect(packet, myRect, WithAlpha(baseColor, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    if (DrawBorder) {
        // Simple 1px border — four thin rects around the edge.
        const Color bc = WithAlpha(skin.PanelBorder, effectiveOpacity);
        const f32   t  = 1.0f;
        PushRect(packet, {myRect.X, myRect.Y, myRect.W, t}, bc, whiteTex, whiteSampler, FillLayer(*this, skin));
        PushRect(packet, {myRect.X, myRect.Y + myRect.H - t, myRect.W, t}, bc, whiteTex, whiteSampler,
                 FillLayer(*this, skin));
        PushRect(packet, {myRect.X, myRect.Y, t, myRect.H}, bc, whiteTex, whiteSampler, FillLayer(*this, skin));
        PushRect(packet, {myRect.X + myRect.W - t, myRect.Y, t, myRect.H}, bc, whiteTex, whiteSampler,
                 FillLayer(*this, skin));
    }
}

void Scrim::OnCollect(FramePacket&        packet, const UIRect myRect, const f32 effectiveOpacity,
                      const TextureHandle whiteTex,
                      const SamplerHandle whiteSampler, const UISkin& skin)
{
    Collect(packet, myRect, effectiveOpacity, whiteTex, whiteSampler, skin, skin.ScrimColor);
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

void Label::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                      TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                      const UISkin& skin)
{
    if (!skin.Font || Text.empty()) return;

    const Color col = WithAlpha(Enabled ? skin.TextLabel : skin.TextDisabled, effectiveOpacity);

    f32 textX = myRect.X;
    if (Align == TextAlign::Center || AutoCenter) textX = myRect.X + myRect.W * 0.5f;
    else if (Align == TextAlign::Right) textX = myRect.X + myRect.W;

    const std::string& display = WordWrap
                                 ? WrapText(*skin.Font, Text, myRect.W, skin.FontScale)
                                 : Text;
    DrawTextUI(packet, *skin.Font, display, {textX, myRect.Y}, skin.FontScale, col,
               TextLayer(*this, skin), Align);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

void Button::OnUpdate(f32 /*dt*/, const UIRect myRect, const Vec2                            mousePos,
                      const bool               mouseDown, bool /*mouseJustDown*/, const bool mouseJustUp,
                      bool&                    consumed)
{
    if (consumed) {
        _hovered = _pressed = false;
        return;
    }

    _hovered = myRect.Contains(mousePos);
    if (!_hovered) {
        _pressed = false;
        return;
    }

    if (mouseDown) _pressed = true;

    if (mouseJustUp && _pressed) {
        _pressed = false;
        consumed = true;
        PlaySound(_interactSound);
        if (OnClick) OnClick();
    }
}

void Button::OnActivate()
{
    PlaySound(_interactSound);
    if (OnClick) OnClick();
}

void Button::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin&       skin)
{
    _interactSound = skin.SoundActivate;
    Color bg;
    if (!Enabled) bg = skin.ButtonDisabled;
    else if (_pressed) bg = skin.ButtonPressed;
    else if (_hovered || _focused) bg = skin.ButtonHovered;
    else bg                           = skin.ButtonNormal;

    PushRect(packet, myRect, WithAlpha(bg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    if (skin.Font && !Text.empty()) {
        const Color col = WithAlpha(Enabled ? skin.TextLabel : skin.TextDisabled, effectiveOpacity);

        const Vec2 pos = {
            myRect.X + myRect.W * 0.5f, myRect.Y + myRect.H * 0.5f
            - skin.Font->GetAscent() * skin.FontScale * 0.5f
        };
        DrawTextUI(packet, *skin.Font, Text, pos, skin.FontScale, col,
                   TextLayer(*this, skin), TextAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

void Image::OnCollect(FramePacket& packet, UIRect myRect, const f32 effectiveOpacity,
                      TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                      const UISkin& skin)
{
    if (!Texture.IsValid()) return;

    Sprite s{};
    s.Texture  = Texture;
    s.Sampler  = Sampler;
    s.Position = {myRect.X + myRect.W * 0.5f, myRect.Y + myRect.H * 0.5f};
    s.Size     = {myRect.W, myRect.H};
    s.UV       = UV;
    s.Tint     = WithAlpha(Tint, effectiveOpacity);
    s.Layer    = FillLayer(*this, skin);
    packet.UISprites.push_back(s);
}

// ---------------------------------------------------------------------------
// NineSlice
// ---------------------------------------------------------------------------

void NineSlice::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                          TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                          const UISkin& skin)
{
    if (!Texture.IsValid()) return;
    PushNineSlice(packet, myRect, WithAlpha(Tint, effectiveOpacity), Texture, Sampler,
                  FillLayer(*this, skin),
                  UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom,
                  PixelLeft, PixelRight, PixelTop, PixelBottom);
}

// ---------------------------------------------------------------------------
// NineSliceButton
// ---------------------------------------------------------------------------

void NineSliceButton::OnUpdate(f32 /*dt*/, const UIRect myRect, const Vec2                            mousePos,
                               const bool               mouseDown, bool /*mouseJustDown*/, const bool mouseJustUp,
                               bool&                    consumed)
{
    if (consumed) {
        _hovered = _pressed = false;
        return;
    }
    _hovered = myRect.Contains(mousePos);
    if (!_hovered) {
        _pressed = false;
        return;
    }
    if (mouseDown) _pressed = true;
    if (mouseJustUp && _pressed) {
        _pressed = false;
        consumed = true;
        PlaySound(_interactSound);
        if (OnClick) OnClick();
    }
}

void NineSliceButton::OnActivate()
{
    PlaySound(_interactSound);
    if (OnClick) OnClick();
}

void NineSliceButton::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                                TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                                const UISkin& skin)
{
    _interactSound = skin.SoundActivate;
    if (Texture.IsValid()) {
        Color tint;
        if (!Enabled) tint = TintDisabled;
        else if (_pressed) tint = TintPressed;
        else if (_hovered || _focused) tint = TintHovered;
        else tint                           = TintNormal;

        PushNineSlice(packet, myRect, WithAlpha(tint, effectiveOpacity), Texture, Sampler,
                      BgLayer(*this, skin),
                      UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom,
                      PixelLeft, PixelRight, PixelTop, PixelBottom);
    }

    if (skin.Font && !Text.empty()) {
        const Color col = WithAlpha(Enabled ? skin.TextLabel : skin.TextDisabled, effectiveOpacity);
        const Vec2  pos = {
            myRect.X + myRect.W * 0.5f,
            myRect.Y + myRect.H * 0.5f - skin.Font->GetAscent() * skin.FontScale * 0.5f
        };
        DrawTextUI(packet, *skin.Font, Text, pos, skin.FontScale, col,
                   TextLayer(*this, skin), TextAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// NineSliceProgressBar
// ---------------------------------------------------------------------------

void NineSliceProgressBar::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                                     TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                                     const UISkin& skin)
{
    if (TrackTexture.IsValid())
        PushNineSlice(packet, myRect, WithAlpha(TrackTint, effectiveOpacity),
                      TrackTexture, TrackSampler, BgLayer(*this, skin),
                      UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom,
                      PixelLeft, PixelRight, PixelTop, PixelBottom);

    if (FillTexture.IsValid()) {
        const f32    fillW = myRect.W * std::clamp(Value, 0.0f, 1.0f);
        const UIRect fillR = {myRect.X, myRect.Y, fillW, myRect.H};
        PushNineSlice(packet, fillR, WithAlpha(FillTint, effectiveOpacity),
                      FillTexture, FillSampler, FillLayer(*this, skin),
                      UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom,
                      PixelLeft, PixelRight, PixelTop, PixelBottom);
    }
}

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------

void ProgressBar::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                            const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                            const UISkin&       skin)
{
    PushRect(packet, myRect, WithAlpha(skin.ProgressBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    const f32    fillW = myRect.W * std::clamp(Value, 0.0f, 1.0f);
    const UIRect fillR = {myRect.X, myRect.Y, fillW, myRect.H};
    PushRect(packet, fillR, WithAlpha(skin.ProgressFill, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}

// ---------------------------------------------------------------------------
// ScrollPanel
// ---------------------------------------------------------------------------

UIRect ScrollPanel::ThumbRect(const UIRect track, const f32 panelH) const
{
    const f32 maxScroll = MaxScroll(panelH);
    const f32 viewRatio = std::min(1.0f, panelH / ContentHeight);
    const f32 thumbH    = std::max(20.0f, track.H * viewRatio);
    const f32 thumbT    = (maxScroll > 0.0f)
                          ? (ScrollOffset / maxScroll) * (track.H - thumbH)
                          : 0.0f;
    return {track.X, track.Y + thumbT, track.W, thumbH};
}

UIRect ScrollPanel::ChildRect(const UIRect myRect) const
{
    const bool scrollbar = ContentHeight > myRect.H && ScrollbarWidth > 0.0f;
    return { myRect.X, myRect.Y - ScrollOffset, myRect.W - (scrollbar ? ScrollbarWidth : 0.0f), ContentHeight };
}

void ScrollPanel::KeepFocusInView(const UIRect myRect)
{
    constexpr f32 Margin = 8.0f;
    const auto    it     = std::ranges::find_if(_children, [](const auto& c) { return c->IsFocused(); });
    const UIWidget* focused = it != _children.end() ? it->get() : nullptr;
    if (focused == _shownFocus) return; // only on a focus change, so the wheel can still scroll away
    _shownFocus = focused;
    if (!focused) return;

    const UIRect r = focused->ComputeRect(ChildRect(myRect));
    if (r.Y < myRect.Y) ScrollOffset -= myRect.Y - r.Y + Margin;
    else if (r.Y + r.H > myRect.Y + myRect.H) ScrollOffset += r.Y + r.H - (myRect.Y + myRect.H) + Margin;
    ScrollOffset = std::clamp(ScrollOffset, 0.0f, MaxScroll(myRect.H));
}

void ScrollPanel::UpdateTree(const f32  dt, const UIRect      parent, const Vec2        mousePos,
                             const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp,
                             bool&      consumed, const f32   scrollDelta)
{
    if (!Visible) return;
    const UIRect myRect = ComputeRect(parent);
    _needsScrollbar     = ContentHeight > myRect.H && ScrollbarWidth > 0.0f;

    KeepFocusInView(myRect);

    // Children receive input only when the cursor is inside the content area.
    const UIRect content    = ChildRect(myRect);
    const Vec2   childMouse = myRect.Contains(mousePos) ? mousePos : Vec2{-9999.0f, -9999.0f};
    const f32    maxScroll  = MaxScroll(myRect.H);

    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::ranges::sort(sorted, [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    // Pass 0 scroll to children — only this panel reacts to the wheel.
    for (UIWidget* child : sorted)
        child->UpdateTree(dt, content, childMouse, mouseDown, mouseJustDown, mouseJustUp, consumed);

    // Mouse wheel scroll — apply when cursor is over the panel.
    if (scrollDelta != 0.0f && myRect.Contains(mousePos)) {
        constexpr f32 ScrollSpeed = 48.0f;
        ScrollOffset              = std::clamp(ScrollOffset - scrollDelta * ScrollSpeed, 0.0f, maxScroll);
    }

    // Scrollbar interaction (inlined to avoid redundant rect recompute)
    if (!_needsScrollbar) {
        _dragging = _thumbHovered = false;
        return;
    }
    const UIRect track = {myRect.X + myRect.W - ScrollbarWidth, myRect.Y, ScrollbarWidth, myRect.H};
    const UIRect thumb = ThumbRect(track, myRect.H);
    _thumbHovered      = thumb.Contains(mousePos);

    if (mouseJustDown && _thumbHovered) {
        _dragging        = true;
        _dragStartY      = mousePos.Y;
        _dragStartOffset = ScrollOffset;
    }
    if (mouseJustUp) _dragging = false;
    if (_dragging && mouseDown) {
        const f32 trackH = track.H - thumb.H;
        const f32 ratio  = trackH > 0.0f ? maxScroll / trackH : 0.0f;
        ScrollOffset     = std::clamp(_dragStartOffset + (mousePos.Y - _dragStartY) * ratio, 0.0f, maxScroll);
    }
}

void ScrollPanel::CollectTree(FramePacket&        packet, const UIRect          parent, const f32 parentOpacity,
                              const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                              const UISkin&       skin)
{
    if (!Visible) return;
    const UIRect myRect        = ComputeRect(parent);
    const f32    alpha         = Opacity * parentOpacity;
    const UISkin effectiveSkin = GetEffectiveSkin(skin);
    _needsScrollbar            = ContentHeight > myRect.H && ScrollbarWidth > 0.0f;

    OnCollect(packet, myRect, alpha, whiteTex, whiteSampler, effectiveSkin);

    if (_children.empty()) return;

    const UIRect content = ChildRect(myRect);

    const u32 firstChildSprite = static_cast<u32>(packet.UISprites.size());

    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::ranges::sort(sorted, [](const UIWidget* a, const UIWidget* b) { return a->ZOrder < b->ZOrder; });
    for (UIWidget* child : sorted)
        child->CollectTree(packet, content, alpha, whiteTex, whiteSampler, skin);

    // Register the clip rect (content area only, excluding scrollbar gutter).
    const UIRect clipRect = {myRect.X, myRect.Y, content.W, myRect.H}; // the content width: not over the scrollbar
    packet.UIClipRects.push_back(clipRect);
    const u16 clipIdx = static_cast<u16>(packet.UIClipRects.size()); // 1-based

    for (u32 i = firstChildSprite; i < static_cast<u32>(packet.UISprites.size()); ++i)
        if (packet.UISprites[i].ClipIndex == 0)
            packet.UISprites[i].ClipIndex = clipIdx;
}

void ScrollPanel::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                            const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                            const UISkin&       skin)
{
    PushRect(packet, myRect, WithAlpha(skin.PanelBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    if (!_needsScrollbar) return;

    // Scrollbar track
    const UIRect track = {myRect.X + myRect.W - ScrollbarWidth, myRect.Y, ScrollbarWidth, myRect.H};
    PushRect(packet, track, WithAlpha(skin.ScrollTrack, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));

    // Scrollbar thumb
    const UIRect thumb      = ThumbRect(track, myRect.H);
    const Color  thumbColor = _thumbHovered ? skin.ScrollThumbHovered : skin.ScrollThumb;
    PushRect(packet, thumb, WithAlpha(thumbColor, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}
} // namespace Arcbit
