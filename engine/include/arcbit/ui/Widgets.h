#pragma once

#include <arcbit/ui/UIWidget.h>
#include <arcbit/render/Font.h>
#include <arcbit/core/Math.h>

#include <functional>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Panel — solid background rect; can hold any child widgets.
// ---------------------------------------------------------------------------
class Panel : public UIWidget
{
public:
    bool DrawBorder = false;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void Collect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                 TextureHandle whiteTex, SamplerHandle whiteSampler,
                 const UISkin& skin, Color             baseColor) const;
};

class Scrim : public Panel
{
public:
    Scrim()
    {
        SizePercent = {1.0f, 1.0f};
        Anchor      = {0.0f, 0.0f};
    }

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// Label — single- or multi-line text.
// ---------------------------------------------------------------------------
class Label : public UIWidget
{
public:
    std::string Text;
    TextAlign   Align      = TextAlign::Left;
    bool        WordWrap   = false; // wrap lines at word boundaries to fit myRect.W
    bool        AutoCenter = false;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// Button — clickable rect with label; fires OnClick on mouse-up inside.
// ---------------------------------------------------------------------------
class Button : public UIWidget
{
public:
    std::string           Text;
    std::function<void()> OnClick;

    Button() { Focusable = true; }

protected:
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnActivate() override; // fires sound + OnClick

private:
    bool _hovered = false;
    bool _pressed = false;
};

// ---------------------------------------------------------------------------
// Image — renders an arbitrary texture scaled to the widget rect.
// ---------------------------------------------------------------------------
class Image : public UIWidget
{
public:
    TextureHandle Texture;
    SamplerHandle Sampler;
    Color         Tint     = {1, 1, 1, 1};
    UVRect        UV       = {0, 0, 1, 1};
    f32           Rotation = 0.0f; // radians, clockwise around the center (a spinner)

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// NineSlice — a static panel drawn from a 9-patch texture.
//
// The texture is divided into a 3×3 grid by UV-space border fractions.
// Corners are emitted at PixelBorder* sizes; the center stretches to fill.
// Can hold child widgets. For an interactive nine-slice, use NineSliceButton.
// ---------------------------------------------------------------------------
class NineSlice : public UIWidget
{
public:
    TextureHandle Texture;
    SamplerHandle Sampler;
    Color         Tint = {1, 1, 1, 1};

    // Source UV-space borders (fraction of texture, 0–1).
    f32 UVBorderLeft   = 0.25f;
    f32 UVBorderRight  = 0.25f;
    f32 UVBorderTop    = 0.25f;
    f32 UVBorderBottom = 0.25f;

    // Rendered border sizes in screen pixels.
    f32 PixelLeft   = 16.0f;
    f32 PixelRight  = 16.0f;
    f32 PixelTop    = 16.0f;
    f32 PixelBottom = 16.0f;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// NineSliceButton — interactive button drawn from a 9-patch texture.
//
// A single texture covers all states; per-state tint colors provide visual
// feedback. Texture/Sampler must be assigned in code — the loader cannot
// resolve asset paths yet.
// ---------------------------------------------------------------------------
class NineSliceButton : public UIWidget
{
public:
    TextureHandle Texture;
    SamplerHandle Sampler;

    // Per-state tints. Default white = use artwork colors unchanged.
    Color TintNormal   = {1.0f, 1.0f, 1.0f, 1.0f};
    Color TintHovered  = {0.85f, 0.85f, 0.85f, 1.0f};
    Color TintPressed  = {0.70f, 0.70f, 0.70f, 1.0f};
    Color TintDisabled = {0.50f, 0.50f, 0.50f, 1.0f};

    // Source UV-space borders (fraction of texture, 0–1).
    f32 UVBorderLeft   = 0.25f;
    f32 UVBorderRight  = 0.25f;
    f32 UVBorderTop    = 0.25f;
    f32 UVBorderBottom = 0.25f;

    // Rendered border sizes in screen pixels.
    f32 PixelLeft   = 16.0f;
    f32 PixelRight  = 16.0f;
    f32 PixelTop    = 16.0f;
    f32 PixelBottom = 16.0f;

    std::string           Text;
    std::function<void()> OnClick;

    NineSliceButton() { Focusable = true; }

protected:
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    void OnActivate() override; // fires sound + OnClick

private:
    bool _hovered = false;
    bool _pressed = false;
};

// ---------------------------------------------------------------------------
// NineSliceProgressBar — horizontal fill bar drawn from 9-patch textures.
//
// Track (background) and fill (foreground) each use an independent texture.
// The fill nine-slice is rendered at value * width so the right cap tracks
// the fill edge naturally. Texture/Sampler must be assigned in code.
// ---------------------------------------------------------------------------
class NineSliceProgressBar : public UIWidget
{
public:
    f32 Value = 0.5f; // 0 = empty, 1 = full

    TextureHandle TrackTexture;
    SamplerHandle TrackSampler;
    Color         TrackTint = {1.0f, 1.0f, 1.0f, 1.0f};

    TextureHandle FillTexture;
    SamplerHandle FillSampler;
    Color         FillTint = {1.0f, 1.0f, 1.0f, 1.0f};

    // UV-space borders shared between track and fill (fraction of texture, 0–1).
    f32 UVBorderLeft   = 0.25f;
    f32 UVBorderRight  = 0.25f;
    f32 UVBorderTop    = 0.25f;
    f32 UVBorderBottom = 0.25f;

    // Rendered border sizes in screen pixels.
    f32 PixelLeft   = 8.0f;
    f32 PixelRight  = 8.0f;
    f32 PixelTop    = 8.0f;
    f32 PixelBottom = 8.0f;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// ProgressBar — horizontal fill bar (value in [0, 1]).
// ---------------------------------------------------------------------------
class ProgressBar : public UIWidget
{
public:
    f32 Value = 0.5f; // 0 = empty, 1 = full

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// ScrollPanel — clipped container with a scrollable content area.
//
// Children are positioned in a content rect that scrolls vertically.
// A scrollbar on the right side appears when ContentHeight > the panel height.
// Set ContentHeight to the total height of all children before pushing.
// ---------------------------------------------------------------------------
class ScrollPanel : public UIWidget
{
public:
    f32 ContentHeight  = 0.0f; // total logical height of all children; set by the caller
    f32 ScrollOffset   = 0.0f; // current scroll position in pixels (clamped on use)
    f32 ScrollbarWidth = 6.0f; // width of the scrollbar gutter in pixels

protected:
    void UpdateTree(f32   dt, UIRect      parent, Vec2        mousePos,
                    bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                    bool& consumed, f32   scrollDelta = 0.0f) override;

    void CollectTree(FramePacket&  packet, UIRect          parent, f32 parentOpacity,
                     TextureHandle whiteTex, SamplerHandle whiteSampler,
                     const UISkin& skin) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    // The content area: scrolled by ScrollOffset, narrowed by the scrollbar.
    [[nodiscard]] UIRect ChildRect(UIRect myRect) const override;
    [[nodiscard]] bool   ClipsChildren() const override { return true; }

private:
    bool _dragging        = false;
    bool _thumbHovered    = false;
    f32  _dragStartY      = 0.0f;
    f32  _dragStartOffset = 0.0f;
    bool _needsScrollbar  = false; // set in CollectTree / UpdateTree before OnCollect

    const UIWidget* _shownFocus = nullptr; // the focused child last scrolled into view

    [[nodiscard]] f32    MaxScroll(f32 panelH) const { return std::max(0.0f, ContentHeight - panelH); }

    // When focus moves to one of the children (D-pad / arrow navigation),
    // scroll it into view - otherwise focus walks off the visible part.
    void KeepFocusInView(UIRect myRect);

    [[nodiscard]] UIRect ThumbRect(UIRect track, f32 panelH) const;
};
} // namespace Arcbit
