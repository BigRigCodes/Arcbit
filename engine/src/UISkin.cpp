#include <arcbit/ui/UISkin.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>

namespace Arcbit
{
namespace
{
    Color ColorFromJson(const nlohmann::json& j, const Color fallback)
    {
        if (!j.is_array() || j.size() < 4) return fallback;
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }

    Color GetColor(const nlohmann::json& obj, const char* key, const Color fallback)
    {
        if (!obj.contains(key)) return fallback;
        return ColorFromJson(obj[key], fallback);
    }

    nlohmann::json ColorToJson(const Color& c) { return {c.R, c.G, c.B, c.A}; }
} // anonymous namespace

UISkin UISkin::LoadFromFile(const std::string_view path)
{
    const std::string pathStr(path);
    std::ifstream     file(pathStr);
    if (!file.is_open()) {
        LOG_WARN(Engine, "UISkin: could not open '{}'", path);
        return Default();
    }

    nlohmann::json j;
    try { j = nlohmann::json::parse(file); }
    catch (const std::exception& e) {
        LOG_WARN(Engine, "UISkin: parse error in '{}': {}", path, e.what());
        return Default();
    }

    UISkin skin;
    skin.FontScale = j.value("fontScale", 1.0f);
    if (const auto it = j.find("text_style"); it != j.end()) skin.Text = ReadTextStyle(*it, skin.Text);

    if (j.contains("panel")) {
        const auto& p    = j["panel"];
        skin.PanelBg     = GetColor(p, "background", skin.PanelBg);
        skin.PanelBorder = GetColor(p, "border", skin.PanelBorder);
    }
    if (j.contains("button")) {
        const auto& b       = j["button"];
        skin.ButtonNormal   = GetColor(b, "normal", skin.ButtonNormal);
        skin.ButtonHovered  = GetColor(b, "hovered", skin.ButtonHovered);
        skin.ButtonPressed  = GetColor(b, "pressed", skin.ButtonPressed);
        skin.ButtonDisabled = GetColor(b, "disabled", skin.ButtonDisabled);
    }
    if (j.contains("text")) {
        const auto& t     = j["text"];
        skin.TextNormal   = GetColor(t, "normal", skin.TextNormal);
        skin.TextDisabled = GetColor(t, "disabled", skin.TextDisabled);
        skin.TextLabel    = GetColor(t, "label", skin.TextLabel);
    }
    if (j.contains("progressBar")) {
        const auto& pb    = j["progressBar"];
        skin.ProgressBg   = GetColor(pb, "background", skin.ProgressBg);
        skin.ProgressFill = GetColor(pb, "fill", skin.ProgressFill);
    }
    if (j.contains("scrollBar")) {
        const auto& sb          = j["scrollBar"];
        skin.ScrollTrack        = GetColor(sb, "track", skin.ScrollTrack);
        skin.ScrollThumb        = GetColor(sb, "thumb", skin.ScrollThumb);
        skin.ScrollThumbHovered = GetColor(sb, "thumbHovered", skin.ScrollThumbHovered);
    }
    if (j.contains("input")) {
        const auto& in        = j["input"];
        skin.InputBg          = GetColor(in, "background", skin.InputBg);
        skin.InputBorder      = GetColor(in, "border", skin.InputBorder);
        skin.InputFocusBorder = GetColor(in, "focusBorder", skin.InputFocusBorder);
        skin.InputCursor      = GetColor(in, "cursor", skin.InputCursor);
        skin.InputPlaceholder = GetColor(in, "placeholder", skin.InputPlaceholder);
    }
    if (j.contains("slider")) {
        const auto& sl          = j["slider"];
        skin.SliderThumb        = GetColor(sl, "thumb", skin.SliderThumb);
        skin.SliderThumbHovered = GetColor(sl, "thumbHovered", skin.SliderThumbHovered);
    }
    if (j.contains("checkbox")) {
        const auto& cb       = j["checkbox"];
        skin.CheckboxBg      = GetColor(cb, "background", skin.CheckboxBg);
        skin.CheckboxHovered = GetColor(cb, "hovered", skin.CheckboxHovered);
        skin.CheckboxCheck   = GetColor(cb, "check", skin.CheckboxCheck);
    }
    if (j.contains("switch")) {
        const auto& sw   = j["switch"];
        skin.SwitchOn    = GetColor(sw, "on", skin.SwitchOn);
        skin.SwitchOff   = GetColor(sw, "off", skin.SwitchOff);
        skin.SwitchThumb = GetColor(sw, "thumb", skin.SwitchThumb);
    }
    if (j.contains("tooltip")) {
        const auto& t      = j["tooltip"];
        skin.TooltipBg     = GetColor(t, "background", skin.TooltipBg);
        skin.TooltipBorder = GetColor(t, "border", skin.TooltipBorder);
        skin.TooltipDelay  = t.value("delay", skin.TooltipDelay);
        skin.TooltipWidth  = t.value("width", skin.TooltipWidth);
    }
    if (j.contains("drop")) {
        skin.DropAccept = GetColor(j["drop"], "accept", skin.DropAccept);
        skin.DropRefuse = GetColor(j["drop"], "refuse", skin.DropRefuse);
    }
    skin.AccentColor = GetColor(j, "accentColor", skin.AccentColor);
    // Accept legacy "overlayColor" key as a fallback so existing skin files keep working.
    skin.ScrimColor = GetColor(j, "scrimColor",
                               GetColor(j, "overlayColor", skin.ScrimColor));

    if (j.contains("sounds")) {
        const auto& s = j["sounds"];
        if (s.contains("focusMove"))  skin.SoundFocusMove  = s["focusMove"].get<std::string>();
        if (s.contains("activate"))   skin.SoundActivate   = s["activate"].get<std::string>();
        if (s.contains("back"))       skin.SoundBack        = s["back"].get<std::string>();
        if (s.contains("sliderTick")) skin.SoundSliderTick  = s["sliderTick"].get<std::string>();
        if (s.contains("toggle"))     skin.SoundToggle      = s["toggle"].get<std::string>();
    }

    LOG_DEBUG(Engine, "UISkin: loaded '{}'", path);
    return skin;
}

bool UISkin::SaveToFile(const std::string_view path) const
{
    nlohmann::json j;
    j["fontScale"] = FontScale;
    if (!(Text == TextStyle{})) j["text_style"] = TextStyleToJson(Text);
    j["panel"]     = {{"background", ColorToJson(PanelBg)}, {"border", ColorToJson(PanelBorder)}};
    j["button"]    = {
        {"normal", ColorToJson(ButtonNormal)}, {"hovered", ColorToJson(ButtonHovered)},
        {"pressed", ColorToJson(ButtonPressed)}, {"disabled", ColorToJson(ButtonDisabled)}
    };
    j["text"] = {
        {"normal", ColorToJson(TextNormal)}, {"disabled", ColorToJson(TextDisabled)},
        {"label", ColorToJson(TextLabel)}
    };
    j["progressBar"] = {{"background", ColorToJson(ProgressBg)}, {"fill", ColorToJson(ProgressFill)}};
    j["scrollBar"]   = {
        {"track", ColorToJson(ScrollTrack)},
        {"thumb", ColorToJson(ScrollThumb)},
        {"thumbHovered", ColorToJson(ScrollThumbHovered)}
    };
    j["input"] = {
        {"background", ColorToJson(InputBg)}, {"border", ColorToJson(InputBorder)},
        {"focusBorder", ColorToJson(InputFocusBorder)}, {"cursor", ColorToJson(InputCursor)},
        {"placeholder", ColorToJson(InputPlaceholder)}
    };
    j["slider"]   = {{"thumb", ColorToJson(SliderThumb)}, {"thumbHovered", ColorToJson(SliderThumbHovered)}};
    j["checkbox"] = {
        {"background", ColorToJson(CheckboxBg)}, {"hovered", ColorToJson(CheckboxHovered)},
        {"check", ColorToJson(CheckboxCheck)}
    };
    j["switch"] = {{"on", ColorToJson(SwitchOn)}, {"off", ColorToJson(SwitchOff)}, {"thumb", ColorToJson(SwitchThumb)}};
    j["tooltip"] = { {"background", ColorToJson(TooltipBg)}, {"border", ColorToJson(TooltipBorder)},
                     {"delay", TooltipDelay}, {"width", TooltipWidth} };
    j["drop"]    = { {"accept", ColorToJson(DropAccept)}, {"refuse", ColorToJson(DropRefuse)} };
    j["accentColor"] = ColorToJson(AccentColor);
    j["scrimColor"]  = ColorToJson(ScrimColor);
    j["sounds"] = {
        {"focusMove",  SoundFocusMove},
        {"activate",   SoundActivate},
        {"back",       SoundBack},
        {"sliderTick", SoundSliderTick},
        {"toggle",     SoundToggle}
    };

    const std::string pathStr(path);
    std::ofstream     file(pathStr);
    if (!file.is_open()) {
        LOG_WARN(Engine, "UISkin: could not write '{}'", path);
        return false;
    }
    file << j.dump(2);
    return true;
}
TextStyle ReadTextStyle(const nlohmann::json& j, TextStyle style)
{
    if (!j.is_object()) return style;
    style.Bold   = j.value("bold", style.Bold);
    style.Italic = j.value("italic", style.Italic);
    if (const auto it = j.find("outline"); it != j.end()) {
        if (it->is_boolean()) style.Outline = it->get<bool>();
        else if (it->is_number()) style.OutlineWidth = it->get<f32>(), style.Outline = style.OutlineWidth > 0.0f;
    }
    style.OutlineWidth = j.value("outline_width", style.OutlineWidth);
    style.OutlineColor = GetColor(j, "outline_color", style.OutlineColor);
    style.Shadow       = j.value("shadow", style.Shadow);
    if (const auto it = j.find("shadow_offset"); it != j.end() && it->is_array() && it->size() == 2)
        style.ShadowOffset = { (*it)[0].get<f32>(), (*it)[1].get<f32>() };
    style.ShadowColor    = GetColor(j, "shadow_color", style.ShadowColor);
    style.ShadowSoftness = j.value("shadow_softness", style.ShadowSoftness);
    return style;
}

nlohmann::json TextStyleToJson(const TextStyle& style)
{
    return { { "bold", style.Bold },
             { "italic", style.Italic },
             { "outline", style.Outline },
             { "outline_width", style.OutlineWidth },
             { "outline_color", ColorToJson(style.OutlineColor) },
             { "shadow", style.Shadow },
             { "shadow_offset", { style.ShadowOffset.X, style.ShadowOffset.Y } },
             { "shadow_color", ColorToJson(style.ShadowColor) },
             { "shadow_softness", style.ShadowSoftness } };
}

} // namespace Arcbit
