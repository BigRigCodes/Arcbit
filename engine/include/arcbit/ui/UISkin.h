#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/render/Font.h>
#include <arcbit/render/RichText.h>

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Arcbit
{
// Shared visual theme passed to every widget during collect.
// All colors are RGBA (0-1 range). Load from JSON with LoadFromFile().
struct UISkin
{
    const FontAtlas* Font      = nullptr; // must be set after loading; not serialized
    f32              FontScale = 1.0f;
    TextStyle        Text;                // how labels and buttons draw their text: bold, outline, shadow

    // Panel
    Color PanelBg     = {0.10f, 0.10f, 0.12f, 0.90f};
    Color PanelBorder = {0.30f, 0.30f, 0.35f, 1.00f};

    // Button states
    Color ButtonNormal   = {0.20f, 0.20f, 0.25f, 1.00f};
    Color ButtonHovered  = {0.30f, 0.30f, 0.38f, 1.00f};
    Color ButtonPressed  = {0.14f, 0.14f, 0.18f, 1.00f};
    Color ButtonDisabled = {0.15f, 0.15f, 0.15f, 0.60f};

    // Text
    Color TextNormal   = {1.00f, 1.00f, 1.00f, 1.00f};
    Color TextDisabled = {0.50f, 0.50f, 0.50f, 1.00f};
    Color TextLabel    = {0.85f, 0.85f, 0.85f, 1.00f};

    // Progress bar
    Color ProgressBg   = {0.15f, 0.15f, 0.15f, 1.00f};
    Color ProgressFill = {0.20f, 0.65f, 0.25f, 1.00f};

    // Scroll panel
    Color ScrollTrack        = {0.08f, 0.08f, 0.10f, 1.00f};
    Color ScrollThumb        = {0.30f, 0.30f, 0.38f, 1.00f};
    Color ScrollThumbHovered = {0.45f, 0.45f, 0.55f, 1.00f};

    // Accent — used for focused/listening states (e.g. rebind screen row)
    Color AccentColor = {0.38f, 0.60f, 0.90f, 1.00f};

    // Scrim — full-screen darkening overlay rendered behind modal screens
    Color ScrimColor = {0.10f, 0.10f, 0.12f, 0.20f};

    // Text input
    Color InputBg          = {0.08f, 0.08f, 0.10f, 1.00f};
    Color InputBorder      = {0.30f, 0.30f, 0.35f, 1.00f};
    Color InputFocusBorder = {0.38f, 0.60f, 0.90f, 1.00f};
    Color InputCursor      = {0.38f, 0.60f, 0.90f, 1.00f};
    Color InputPlaceholder = {0.40f, 0.40f, 0.45f, 1.00f};

    // Slider
    Color SliderThumb        = {0.38f, 0.60f, 0.90f, 1.00f};
    Color SliderThumbHovered = {0.55f, 0.75f, 1.00f, 1.00f};

    // Checkbox and radio button (shared)
    Color CheckboxBg      = {0.20f, 0.20f, 0.25f, 1.00f};
    Color CheckboxHovered = {0.30f, 0.30f, 0.38f, 1.00f};
    Color CheckboxCheck   = {0.38f, 0.60f, 0.90f, 1.00f};

    // Switch
    Color SwitchOn    = {0.20f, 0.55f, 0.25f, 1.00f};
    Color SwitchOff   = {0.20f, 0.20f, 0.25f, 1.00f};
    Color SwitchThumb = {0.90f, 0.90f, 0.95f, 1.00f};

    // Tooltips (screen-wide, not per widget): shown after the pointer rests
    // TooltipDelay seconds on a widget with one, wrapped at TooltipWidth.
    Color TooltipBg     = {0.06f, 0.06f, 0.08f, 0.96f};
    Color TooltipBorder = {0.38f, 0.60f, 0.90f, 1.00f};
    f32   TooltipDelay  = 0.5f;
    f32   TooltipWidth  = 360.0f;

    // Drag and drop: the outline of a target a payload is over - one that
    // takes it, and one that doesn't.
    Color DropAccept = {0.40f, 0.85f, 0.45f, 1.00f}; // apart from the focus accent: a target can be focused too
    Color DropRefuse = {0.85f, 0.30f, 0.30f, 1.00f};

    // Sound keys — resolved via AudioManager at interaction time; empty = no sound.
    // Replaced by asset handle references in Phase 36.
    std::string SoundFocusMove;   // keyboard/gamepad focus navigation
    std::string SoundActivate;    // button confirm / click
    std::string SoundBack;        // cancel / close / pop screen
    std::string SoundSliderTick;  // slider value step
    std::string SoundToggle;      // checkbox / switch toggle

    // Per-screen layer base set by UIManager based on stack index.
    // Ensures sprites from screens higher in the stack always sort above lower ones.
    i32 ScreenLayerBase = 0;

    // Load from a JSON file. Returns Default() if the file cannot be opened or parsed.
    // Does not set Font — assign that separately after loading.
    [[nodiscard]] static UISkin LoadFromFile(std::string_view path);

    // Returns a skin with the default values above.
    [[nodiscard]] static UISkin Default() { return {}; }

    // Write to JSON. Returns false on failure.
    [[nodiscard]] bool SaveToFile(std::string_view path) const;
};

// ---------------------------------------------------------------------------
// UISkinOverride — per-widget skin property overrides.
//
// IMPORTANT: This struct must mirror UISkin's themeable fields. Every field in
// UISkin (except ScreenLayerBase, which is managed internally by UIManager)
// must have a matching std::optional here. When you add a field to UISkin,
// add the corresponding std::optional field to this struct and the merge line
// in UIWidget::GetEffectiveSkin.
//
// Font is set by key: register fonts with UILoader::RegisterFont("key", atlas)
// then reference them in JSON as { "Font": "key" }.
// UIManager::Init auto-registers the engine font as "default".
// ---------------------------------------------------------------------------
struct UISkinOverride
{
    std::optional<const FontAtlas*> Font;
    std::optional<f32>              FontScale;
    std::optional<TextStyle>        Text;

    std::optional<Color> PanelBg;
    std::optional<Color> PanelBorder;

    std::optional<Color> ButtonNormal;
    std::optional<Color> ButtonHovered;
    std::optional<Color> ButtonPressed;
    std::optional<Color> ButtonDisabled;

    std::optional<Color> TextNormal;
    std::optional<Color> TextDisabled;
    std::optional<Color> TextLabel;

    std::optional<Color> ProgressBg;
    std::optional<Color> ProgressFill;

    std::optional<Color> ScrollTrack;
    std::optional<Color> ScrollThumb;
    std::optional<Color> ScrollThumbHovered;

    std::optional<Color> AccentColor;
    std::optional<Color> ScrimColor;

    std::optional<Color> InputBg;
    std::optional<Color> InputBorder;
    std::optional<Color> InputFocusBorder;
    std::optional<Color> InputCursor;
    std::optional<Color> InputPlaceholder;

    std::optional<Color> SliderThumb;
    std::optional<Color> SliderThumbHovered;

    std::optional<Color> CheckboxBg;
    std::optional<Color> CheckboxHovered;
    std::optional<Color> CheckboxCheck;

    std::optional<Color> SwitchOn;
    std::optional<Color> SwitchOff;
    std::optional<Color> SwitchThumb;

    // Note: SoundFocusMove and SoundBack are intentionally NOT overridable per-widget.
    // They are screen/global UX events handled by UIManager from the active skin.
    std::optional<std::string> SoundActivate;
    std::optional<std::string> SoundSliderTick;
    std::optional<std::string> SoundToggle;
};
// A text style from JSON - "text_style" in a skin file or on an .arcui
// widget - on top of style:
//   { "bold": true, "italic": false, "outline": true, "outline_width": 2, "outline_color": [0,0,0,1],
//     "shadow": true, "shadow_offset": [2, 2], "shadow_color": [0,0,0,0.6], "shadow_softness": 1 }
// "outline" may also be a width in pixels (0 = none).
[[nodiscard]] TextStyle      ReadTextStyle(const nlohmann::json& j, TextStyle style);
[[nodiscard]] nlohmann::json TextStyleToJson(const TextStyle& style);

} // namespace Arcbit
