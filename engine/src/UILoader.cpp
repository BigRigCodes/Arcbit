#include <arcbit/ui/UILoader.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <unordered_map>

namespace Arcbit
{
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Module-scope type registry
// ---------------------------------------------------------------------------

namespace
{
    struct TypeEntry
    {
        UILoader::Factory                           factory;
        std::function<void(UIWidget&, const json&)> apply; // may be null for custom types
    };

    std::unordered_map<std::string, TypeEntry>& Registry()
    {
        static std::unordered_map<std::string, TypeEntry> s_map;
        return s_map;
    }

    std::unordered_map<std::string, const FontAtlas*>& FontRegistry()
    {
        static std::unordered_map<std::string, const FontAtlas*> s_map;
        return s_map;
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UILoader::RegisterFont(const std::string_view key, const FontAtlas& font)
{
    FontRegistry()[std::string(key)] = &font;
}

const FontAtlas* UILoader::FindFont(const std::string_view key)
{
    const auto& map = FontRegistry();
    const auto  it  = map.find(std::string(key));
    return it != map.end() ? it->second : nullptr;
}

void UILoader::RegisterType(const std::string_view typeName, Factory factory)
{
    Registry()[std::string(typeName)] = {std::move(factory), nullptr};
}

// ---------------------------------------------------------------------------
// JSON read helpers
// ---------------------------------------------------------------------------

static Vec2 ReadVec2(const json& j, const Vec2 def = {})
{
    if (!j.is_array() || j.size() < 2) return def;
    return {j[0].get<f32>(), j[1].get<f32>()};
}

static Color ReadColor(const json& j, const Color def = {})
{
    if (!j.is_array() || j.size() < 4) return def;
    return {j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>(), j[3].get<f32>()};
}

// ---------------------------------------------------------------------------
// Property appliers — one per widget type
// ---------------------------------------------------------------------------

static void ApplySkinBlock(UISkinOverride& o, const json& j)
{
    // Generic skin override block — keys match UISkin / UISkinOverride field names.
    // NOTE: When adding a field to UISkin / UISkinOverride, add a line here.
    if (auto it = j.find("Font"); it != j.end()) {
        if (const FontAtlas* f = UILoader::FindFont(it->get<std::string>()))
            o.Font = f;
    }
    if (auto it = j.find("FontScale"); it != j.end()) o.FontScale = it->get<f32>();
    if (auto it = j.find("TextStyle"); it != j.end()) o.Text = ReadTextStyle(*it, o.Text.value_or(TextStyle{}));
    if (auto it = j.find("PanelBg"); it != j.end()) o.PanelBg = ReadColor(*it);
    if (auto it = j.find("PanelBorder"); it != j.end()) o.PanelBorder = ReadColor(*it);
    if (auto it = j.find("ButtonNormal"); it != j.end()) o.ButtonNormal = ReadColor(*it);
    if (auto it = j.find("ButtonHovered"); it != j.end()) o.ButtonHovered = ReadColor(*it);
    if (auto it = j.find("ButtonPressed"); it != j.end()) o.ButtonPressed = ReadColor(*it);
    if (auto it = j.find("ButtonDisabled"); it != j.end()) o.ButtonDisabled = ReadColor(*it);
    if (auto it = j.find("TextNormal"); it != j.end()) o.TextNormal = ReadColor(*it);
    if (auto it = j.find("TextDisabled"); it != j.end()) o.TextDisabled = ReadColor(*it);
    if (auto it = j.find("TextLabel"); it != j.end()) o.TextLabel = ReadColor(*it);
    if (auto it = j.find("ProgressBg"); it != j.end()) o.ProgressBg = ReadColor(*it);
    if (auto it = j.find("ProgressFill"); it != j.end()) o.ProgressFill = ReadColor(*it);
    if (auto it = j.find("ScrollTrack"); it != j.end()) o.ScrollTrack = ReadColor(*it);
    if (auto it = j.find("ScrollThumb"); it != j.end()) o.ScrollThumb = ReadColor(*it);
    if (auto it = j.find("ScrollThumbHovered"); it != j.end()) o.ScrollThumbHovered = ReadColor(*it);
    if (auto it = j.find("AccentColor"); it != j.end()) o.AccentColor = ReadColor(*it);
    if (auto it = j.find("ScrimColor"); it != j.end()) o.ScrimColor = ReadColor(*it);
    if (auto it = j.find("InputBg"); it != j.end()) o.InputBg = ReadColor(*it);
    if (auto it = j.find("InputBorder"); it != j.end()) o.InputBorder = ReadColor(*it);
    if (auto it = j.find("InputFocusBorder"); it != j.end()) o.InputFocusBorder = ReadColor(*it);
    if (auto it = j.find("InputCursor"); it != j.end()) o.InputCursor = ReadColor(*it);
    if (auto it = j.find("InputPlaceholder"); it != j.end()) o.InputPlaceholder = ReadColor(*it);
    if (auto it = j.find("SliderThumb"); it != j.end()) o.SliderThumb = ReadColor(*it);
    if (auto it = j.find("SliderThumbHovered"); it != j.end()) o.SliderThumbHovered = ReadColor(*it);
    if (auto it = j.find("CheckboxBg"); it != j.end()) o.CheckboxBg = ReadColor(*it);
    if (auto it = j.find("CheckboxHovered"); it != j.end()) o.CheckboxHovered = ReadColor(*it);
    if (auto it = j.find("CheckboxCheck"); it != j.end()) o.CheckboxCheck = ReadColor(*it);
    if (auto it = j.find("SwitchOn"); it != j.end()) o.SwitchOn = ReadColor(*it);
    if (auto it = j.find("SwitchOff"); it != j.end()) o.SwitchOff = ReadColor(*it);
    if (auto it = j.find("SwitchThumb"); it != j.end()) o.SwitchThumb = ReadColor(*it);
    // SoundFocusMove and SoundBack are global UX events handled by UIManager
    // from the active skin; per-widget overrides aren't supported.
    if (auto it = j.find("SoundActivate");   it != j.end()) o.SoundActivate   = it->get<std::string>();
    if (auto it = j.find("SoundSliderTick"); it != j.end()) o.SoundSliderTick = it->get<std::string>();
    if (auto it = j.find("SoundToggle");     it != j.end()) o.SoundToggle     = it->get<std::string>();
}

static void ApplyBase(UIWidget& w, const json& j)
{
    if (auto it = j.find("name"); it != j.end()) w.Name = it->get<std::string>();
    if (auto it = j.find("size"); it != j.end()) w.Size = ReadVec2(*it, w.Size);
    if (auto it = j.find("size_percent"); it != j.end()) w.SizePercent = ReadVec2(*it);
    if (auto it = j.find("anchor"); it != j.end()) w.Anchor = ReadVec2(*it);
    if (auto it = j.find("pivot"); it != j.end()) w.Pivot = ReadVec2(*it);
    if (auto it = j.find("offset"); it != j.end()) w.Offset = ReadVec2(*it);
    if (auto it = j.find("zorder"); it != j.end()) w.ZOrder = it->get<i32>();
    if (auto it = j.find("opacity"); it != j.end()) w.Opacity = it->get<f32>();
    if (auto it = j.find("visible"); it != j.end()) w.Visible = it->get<bool>();
    if (auto it = j.find("enabled"); it != j.end()) w.Enabled = it->get<bool>();
    if (auto it = j.find("focusable"); it != j.end()) w.Focusable = it->get<bool>();
    if (auto it = j.find("tab_order"); it != j.end()) w.TabOrder = it->get<u32>();
    if (auto it = j.find("text_style"); it != j.end()) w.SkinOverride.Text = ReadTextStyle(*it, w.SkinOverride.Text.value_or(TextStyle{}));
    if (auto it = j.find("skin"); it != j.end()) ApplySkinBlock(w.SkinOverride, *it);
}

static void ApplyPanel(UIWidget& w, const json& j)
{
    auto& p = dynamic_cast<Panel&>(w);
    if (const auto it = j.find("background_color"); it != j.end()) p.SkinOverride.PanelBg = ReadColor(*it);
    if (const auto it = j.find("draw_border"); it != j.end()) p.DrawBorder = it->get<bool>();
}

// Scrim renders from skin.ScrimColor (not PanelBg), so background_color on a
// Scrim must route to ScrimColor — using ApplyPanel here would silently drop the value.
static void ApplyScrim(UIWidget& w, const json& j)
{
    auto& s = dynamic_cast<Scrim&>(w);
    if (const auto it = j.find("background_color"); it != j.end()) s.SkinOverride.ScrimColor = ReadColor(*it);
    if (const auto it = j.find("draw_border"); it != j.end()) s.DrawBorder = it->get<bool>();
}

static void ApplyLabel(UIWidget& w, const json& j)
{
    auto& l = dynamic_cast<Label&>(w);
    if (const auto it = j.find("text"); it != j.end()) l.Text = it->get<std::string>();
    if (const auto it = j.find("text_key"); it != j.end()) l.SetTextKey(it->get<std::string>());
    if (const auto it = j.find("word_wrap"); it != j.end()) l.WordWrap = it->get<bool>();
    if (const auto it = j.find("auto_center"); it != j.end()) l.AutoCenter = it->get<bool>();
    if (const auto it = j.find("markup"); it != j.end()) l.Markup = it->get<bool>();
    if (const auto it = j.find("text_color"); it != j.end()) l.SkinOverride.TextLabel = ReadColor(*it);
    if (const auto it = j.find("align"); it != j.end()) {
        if (const std::string s = it->get<std::string>(); s == "center")
            l.Align = TextAlign::Center;
        else if (s == "right")
            l.Align = TextAlign::Right;
        else
            l.Align = TextAlign::Left;
    }
}

static void ApplyButton(UIWidget& w, const json& j)
{
    auto& b = dynamic_cast<Button&>(w);
    if (const auto it = j.find("text"); it != j.end()) b.Text = it->get<std::string>();
    if (const auto it = j.find("text_key"); it != j.end()) b.SetTextKey(it->get<std::string>());
    if (const auto it = j.find("text_color"); it != j.end()) b.SkinOverride.TextLabel = ReadColor(*it);
}

static void ApplyProgressBar(UIWidget& w, const json& j)
{
    auto& p = dynamic_cast<ProgressBar&>(w);
    if (const auto it = j.find("value"); it != j.end()) p.Value = it->get<f32>();
    if (const auto it = j.find("fill_color"); it != j.end()) p.SkinOverride.ProgressFill = ReadColor(*it);
}

static void ApplyScrollPanel(UIWidget& w, const json& j)
{
    auto& s = dynamic_cast<ScrollPanel&>(w);
    if (const auto it = j.find("content_height"); it != j.end()) s.ContentHeight = it->get<f32>();
    if (const auto it = j.find("scrollbar_width"); it != j.end()) s.ScrollbarWidth = it->get<f32>();
}

// Read the shared UV/pixel border fields that all nine-slice widgets expose.
template <typename T>
static void ApplyNineSliceBorders(T& w, const json& j)
{
    if (auto it = j.find("uv_border_left"); it != j.end()) w.UVBorderLeft = it->get<f32>();
    if (auto it = j.find("uv_border_right"); it != j.end()) w.UVBorderRight = it->get<f32>();
    if (auto it = j.find("uv_border_top"); it != j.end()) w.UVBorderTop = it->get<f32>();
    if (auto it = j.find("uv_border_bottom"); it != j.end()) w.UVBorderBottom = it->get<f32>();
    if (auto it = j.find("pixel_left"); it != j.end()) w.PixelLeft = it->get<f32>();
    if (auto it = j.find("pixel_right"); it != j.end()) w.PixelRight = it->get<f32>();
    if (auto it = j.find("pixel_top"); it != j.end()) w.PixelTop = it->get<f32>();
    if (auto it = j.find("pixel_bottom"); it != j.end()) w.PixelBottom = it->get<f32>();
}

static void ApplyNineSlice(UIWidget& w, const json& j)
{
    auto& n = dynamic_cast<NineSlice&>(w);
    if (auto it = j.find("tint"); it != j.end()) n.Tint = ReadColor(*it);
    ApplyNineSliceBorders(n, j);
}

static void ApplyNineSliceButton(UIWidget& w, const json& j)
{
    auto& b = dynamic_cast<NineSliceButton&>(w);
    if (auto it = j.find("text"); it != j.end()) b.Text = it->get<std::string>();
    if (auto it = j.find("text_key"); it != j.end()) b.SetTextKey(it->get<std::string>());
    if (auto it = j.find("tint_normal"); it != j.end()) b.TintNormal = ReadColor(*it);
    if (auto it = j.find("tint_hovered"); it != j.end()) b.TintHovered = ReadColor(*it);
    if (auto it = j.find("tint_pressed"); it != j.end()) b.TintPressed = ReadColor(*it);
    if (auto it = j.find("tint_disabled"); it != j.end()) b.TintDisabled = ReadColor(*it);
    ApplyNineSliceBorders(b, j);
}

static void ApplyNineSliceProgressBar(UIWidget& w, const json& j)
{
    auto& p = dynamic_cast<NineSliceProgressBar&>(w);
    if (auto it = j.find("value"); it != j.end()) p.Value = it->get<f32>();
    if (auto it = j.find("track_tint"); it != j.end()) p.TrackTint = ReadColor(*it);
    if (auto it = j.find("fill_tint"); it != j.end()) p.FillTint = ReadColor(*it);
    ApplyNineSliceBorders(p, j);
}

static void ApplyImage(UIWidget& w, const json& j)
{
    auto& img = dynamic_cast<Image&>(w);
    if (const auto it = j.find("tint"); it != j.end()) img.Tint = ReadColor(*it);
    if (const auto it = j.find("uv"); it != j.end() && it->is_array() && it->size() >= 4)
        img.UV = {(*it)[0].get<f32>(), (*it)[1].get<f32>(), (*it)[2].get<f32>(), (*it)[3].get<f32>()};
}

static void ApplyTextInput(UIWidget& w, const json& j)
{
    auto& t = dynamic_cast<TextInput&>(w);
    if (const auto it = j.find("placeholder"); it != j.end()) t.Placeholder = it->get<std::string>();
    if (const auto it = j.find("placeholder_key"); it != j.end()) t.PlaceholderKey = it->get<std::string>(), t.Placeholder = Loc::Get(t.PlaceholderKey);
    if (const auto it = j.find("max_length"); it != j.end()) t.MaxLength = it->get<u32>();
    if (const auto it = j.find("pattern"); it != j.end()) t.Pattern = it->get<std::string>();
    if (const auto it = j.find("mode"); it != j.end()) {
        if (const std::string m = it->get<std::string>(); m == "numeric")
            t.InputMode = TextInput::Mode::Numeric;
        else if (m == "regex")
            t.InputMode = TextInput::Mode::Regex;
        else
            t.InputMode = TextInput::Mode::Text;
    }
}

static void ApplySlider(UIWidget& w, const json& j)
{
    auto& s = dynamic_cast<Slider&>(w);
    if (const auto it = j.find("value"); it != j.end()) s.Value = it->get<f32>();
    if (const auto it = j.find("min"); it != j.end()) s.Min = it->get<f32>();
    if (const auto it = j.find("max"); it != j.end()) s.Max = it->get<f32>();
    if (const auto it = j.find("step"); it != j.end()) s.Step = it->get<f32>();
    if (const auto it = j.find("style"); it != j.end()) s.Style = it->get<std::string>() == "bar" ? Slider::Look::Bar : Slider::Look::Line;
}

// "item_keys": the items as keys, resolved now and on every language change.
static void ReadItemKeys(const json& j, std::vector<std::string>& keys, std::vector<std::string>& items)
{
    const auto it = j.find("item_keys");
    if (it == j.end() || !it->is_array()) return;
    keys.clear();
    items.clear();
    for (const auto& key : *it) {
        keys.push_back(key.get<std::string>());
        items.push_back(Loc::Get(keys.back()));
    }
}

static void ApplyDropdown(UIWidget& w, const json& j)
{
    auto& d = dynamic_cast<Dropdown&>(w);
    if (const auto it = j.find("selected"); it != j.end()) d.SelectedIndex = it->get<i32>();
    if (const auto it = j.find("items"); it != j.end() && it->is_array())
        for (const auto& item : *it)
            d.Items.push_back(item.get<std::string>());
    ReadItemKeys(j, d.ItemKeys, d.Items);
    if (const auto it = j.find("filter_threshold"); it != j.end()) d.FilterThreshold = it->get<u32>();
    if (const auto it = j.find("max_visible"); it != j.end()) d.MaxVisibleItems = it->get<u32>();
    if (const auto it = j.find("item_height"); it != j.end()) d.ItemHeight = it->get<f32>();
}

static void ApplyCheckbox(UIWidget& w, const json& j)
{
    auto& c = dynamic_cast<Checkbox&>(w);
    if (const auto it = j.find("checked"); it != j.end()) c.Checked = it->get<bool>();
    if (const auto it = j.find("label"); it != j.end()) c.Label = it->get<std::string>();
    if (const auto it = j.find("label_key"); it != j.end()) c.SetLabelKey(it->get<std::string>());
}

static void ApplyRadioGroup(UIWidget& w, const json& j)
{
    auto& r = dynamic_cast<RadioGroup&>(w);
    if (const auto it = j.find("selected"); it != j.end()) r.SelectedIndex = it->get<i32>();
    if (const auto it = j.find("item_height"); it != j.end()) r.ItemHeight = it->get<f32>();
    if (const auto it = j.find("items"); it != j.end() && it->is_array())
        for (const auto& item : *it)
            r.Items.push_back(item.get<std::string>());
    ReadItemKeys(j, r.ItemKeys, r.Items);
}

static void ApplySwitch(UIWidget& w, const json& j)
{
    auto& s = dynamic_cast<Switch&>(w);
    if (const auto it = j.find("on"); it != j.end()) s.On = it->get<bool>();
    if (const auto it = j.find("anim_speed"); it != j.end()) s.AnimSpeed = it->get<f32>();
}

// ---------------------------------------------------------------------------
// EnsureBuiltins — register all engine widget types once
// ---------------------------------------------------------------------------

void UILoader::EnsureBuiltins()
{
    auto& r = Registry();
    if (!r.empty()) return; // already registered

    r["Panel"]                = {[] { return std::make_unique<Panel>(); }, ApplyPanel};
    r["Scrim"]                = {[] { return std::make_unique<Scrim>(); }, ApplyScrim};
    r["Label"]                = {[] { return std::make_unique<Label>(); }, ApplyLabel};
    r["Button"]               = {[] { return std::make_unique<Button>(); }, ApplyButton};
    r["Image"]                = {[] { return std::make_unique<Image>(); }, ApplyImage};
    r["NineSlice"]            = {[] { return std::make_unique<NineSlice>(); }, ApplyNineSlice};
    r["NineSliceButton"]      = {[] { return std::make_unique<NineSliceButton>(); }, ApplyNineSliceButton};
    r["NineSliceProgressBar"] = {[] { return std::make_unique<NineSliceProgressBar>(); }, ApplyNineSliceProgressBar};
    r["ProgressBar"]          = {[] { return std::make_unique<ProgressBar>(); }, ApplyProgressBar};
    r["ScrollPanel"]          = {[] { return std::make_unique<ScrollPanel>(); }, ApplyScrollPanel};
    r["TextInput"]            = {[] { return std::make_unique<TextInput>(); }, ApplyTextInput};
    r["Slider"]               = {[] { return std::make_unique<Slider>(); }, ApplySlider};
    r["Dropdown"]             = {[] { return std::make_unique<Dropdown>(); }, ApplyDropdown};
    r["Checkbox"]             = {[] { return std::make_unique<Checkbox>(); }, ApplyCheckbox};
    r["RadioGroup"]           = {[] { return std::make_unique<RadioGroup>(); }, ApplyRadioGroup};
    r["Switch"]               = {[] { return std::make_unique<Switch>(); }, ApplySwitch};
}

// ---------------------------------------------------------------------------
// Recursive widget builder
// ---------------------------------------------------------------------------

// "on_click" / "on_change": the Lua function a ScriptedScreen calls.
static void RecordScriptHandlers(UIWidget& widget, const json& j, UIScreen& screen)
{
    for (const auto& [key, event] : { std::pair{ "on_click", "click" }, std::pair{ "on_change", "change" } })
        if (const auto it = j.find(key); it != j.end() && it->is_string())
            screen.AddScriptHandler({ &widget, event, it->get<std::string>() });
}

static std::unique_ptr<UIWidget> BuildWidget(const json& j, UIScreen& screen)
{
    const auto typeIt = j.find("type");
    if (typeIt == j.end()) {
        LOG_WARN(UI, "arcui widget missing 'type' field");
        return nullptr;
    }

    const std::string type    = typeIt->get<std::string>();
    auto&             reg     = Registry();
    const auto        entryIt = reg.find(type);
    if (entryIt == reg.end()) {
        LOG_WARN(UI, "arcui unknown widget type: {}", type);
        return nullptr;
    }

    auto widget = entryIt->second.factory();
    ApplyBase(*widget, j);
    if (entryIt->second.apply) entryIt->second.apply(*widget, j);
    RecordScriptHandlers(*widget, j, screen);

    if (const auto childIt = j.find("children"); childIt != j.end() && childIt->is_array())
        for (const auto& childJson : *childIt)
            if (auto child = BuildWidget(childJson, screen))
                widget->AddChildRaw(std::move(child));

    return widget;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

bool UILoader::Load(const std::string_view path, UIScreen& screen)
{
    EnsureBuiltins();

    std::ifstream file{std::string(path)};
    if (!file.is_open()) {
        LOG_ERROR(UI, "arcui: cannot open '{}'", path);
        return false;
    }

    json root;
    try { root = json::parse(file); }
    catch (const json::exception& e) {
        LOG_ERROR(UI, "arcui: parse error in '{}': {}", path, e.what());
        return false;
    }

    // Build widget tree
    if (const auto widgetsIt = root.find("widgets"); widgetsIt != root.end() && widgetsIt->is_array())
        for (const auto& wJson : *widgetsIt)
            if (auto w = BuildWidget(wJson, screen))
                screen.AddRaw(std::move(w));

    // The Lua script behind a ScriptedScreen (a module name).
    if (const auto scriptIt = root.find("script"); scriptIt != root.end() && scriptIt->is_string())
        screen.SetMetaStr("script", scriptIt->get<std::string>());

    // Populate meta section — numbers become f32, strings become str
    if (const auto metaIt = root.find("meta"); metaIt != root.end() && metaIt->is_object()) {
        for (const auto& [key, val] : metaIt->items()) {
            if (val.is_number()) screen.SetMetaF32(key, val.get<f32>());
            else if (val.is_string()) screen.SetMetaStr(key, val.get<std::string>());
        }
    }

    return true;
}
} // namespace Arcbit
