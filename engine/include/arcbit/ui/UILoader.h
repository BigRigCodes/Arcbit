#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace Arcbit
{
class UIWidget;
class UIScreen;
class FontAtlas;

// ---------------------------------------------------------------------------
// UILoader — deserializes .arcui JSON files into UIScreen widget trees.
//
// All built-in widget types (Panel, Label, Button, etc.) are pre-registered.
// Call RegisterType() to add game-specific custom widget types before any
// Load() call that references them.
//
// .arcui format (JSON):
//   {
//     "widgets":   [ { "type": "Panel", "name": "bg", ... } ],
//     "templates": { "row": { "type": "Panel", ... } },   // stamped out by code
//     "meta":      { "btn_gap": 56, "title": "My Menu" }
//   }
//
// After loading, use UIScreen::FindWidget<T>(name) to retrieve named widgets
// and attach OnClick / other callbacks in code.
// ---------------------------------------------------------------------------
class UILoader
{
public:
    using Factory = std::function<std::unique_ptr<UIWidget>()>;

    // Parse a .arcui file and populate screen's widget tree and meta.
    // Returns false and logs an error if the file cannot be opened or parsed.
    [[nodiscard]] static bool Load(std::string_view path, UIScreen& screen);

    // One widget (and its children) from its .arcui JSON - a layout's
    // template (UIScreen::AddFromTemplate). Null, logged, if it isn't one.
    [[nodiscard]] static std::unique_ptr<UIWidget> Build(std::string_view json, UIScreen& screen);

    // Register a custom widget type. typeName must match the "type" field in
    // .arcui files. Call before any Load() that uses the type.
    static void RegisterType(std::string_view typeName, Factory factory);

    // Register a font under a string key so .arcui files can reference it by
    // name in a skin block: { "Font": "roboto" }.
    // UIManager::Init auto-registers the engine font as "default".
    static void RegisterFont(std::string_view key, const FontAtlas& font);

    // Returns the font registered under key, or nullptr if not found.
    [[nodiscard]] static const FontAtlas* FindFont(std::string_view key);

private:
    static void EnsureBuiltins();
};
} // namespace Arcbit
