#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit
{
// How a one-time notice is shown: a card that waits for OK, a toast that
// fades by itself, or not at all (the engine event still goes out).
enum class NoticeStyle : u8 { Card, Toast, None };

// ---------------------------------------------------------------------------
// ProjectDefaults - the engine behavior a game can change in one place
// (ApplicationConfig::Defaults), until project.arcbit (Phase 38) reads the
// same fields from a file. Most can be changed per instance too; each field
// says where.
// ---------------------------------------------------------------------------
struct ProjectDefaults
{
    // Tap to turn: how long (seconds) a standing tile mover holds a new
    // direction before it walks - a quicker tap only turns it. 0 = off.
    // Per mover: SmoothTileMovement / SnapTileMovement::TurnDelay
    // ("turn_delay").
    f32 TileTurnDelay = 0.1f;

    // Whether Esc / gamepad East on a dialog's line ends the conversation
    // (on a choice it picks the option marked "cancel", either way).
    // Per dialog: "cancelable" in the .arcdialog file.
    bool DialogCancelable = true;

    // Sprites draw in order of their bottom edge's Y, sorting with each other
    // and Objects-layer tiles: a character walks behind a tree and in front
    // of it. Per sprite: SpriteRenderer::YSort ("y_sort"; false for flat
    // things, which keep their Layer).
    bool YSortSprites = true;

    // After an update gave the player new (or reset) actions, the notice of
    // where they landed (Settings::LoadInputBindings), once the boot is
    // done: actions that didn't get their default keys, and resets.
    // engine.input.bindings_updated is published with the same lines.
    NoticeStyle BindingNotice = NoticeStyle::Card;

    // Also list new actions that simply got their default keys.
    bool BindingNoticeAllNew = false;
};
} // namespace Arcbit
