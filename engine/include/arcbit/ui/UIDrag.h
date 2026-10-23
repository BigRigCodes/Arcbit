#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>

#include <any>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Drag and drop (docs/gui.md#drag-and-drop). A widget is a drag source when
// it has OnDragStart, a drop target when it has OnDrop; the screen moves the
// payload between them - with the mouse, the gamepad cursor, or the grab
// key on the focused widget - and draws its ghost under the pointer.
// ---------------------------------------------------------------------------

// What's being dragged. Kind tells targets what it is ("item",
// "quick_slot"); Data carries whatever the source needs to finish the move
// (an index, an item id).
struct DragPayload
{
    std::string Kind;
    std::any    Data;
    u32         Count = 1;

    // A grid item: the rotate key turns it while it's held (Rotated flips,
    // and the ghost's size with it).
    bool Rotatable = false;
    bool Rotated   = false;

    // The ghost under the pointer: an image (none: a block of Tint), its
    // size before turning, and a little text in its corner (a count).
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV   = { 0.0f, 0.0f, 1.0f, 1.0f };
    Color         Tint = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vec2          Size = { 48.0f, 48.0f };
    std::string   Text;

    // The ghost's size as it's held now.
    [[nodiscard]] Vec2 HeldSize() const { return Rotated ? Vec2{ Size.Y, Size.X } : Size; }
};

// How a drag began: by the grab key (Keyboard) rather than the pointer, and
// whether it's the alternate drag - the right mouse button, or Shift with the
// grab key - a source may treat as "split the stack".
struct DragStart
{
    bool Keyboard  = false;
    bool Alternate = false;
};
} // namespace Arcbit
