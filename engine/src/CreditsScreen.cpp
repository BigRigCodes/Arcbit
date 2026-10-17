#include <arcbit/ui/CreditsScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/ui/Widgets.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>

namespace Arcbit
{
namespace
{
    constexpr f32 TitleHeight   = 72.0f;
    constexpr f32 HeadingHeight = 48.0f;
    constexpr f32 NameHeight    = 34.0f;
    constexpr f32 SectionGap    = 40.0f;
    constexpr f32 LineWidth     = 900.0f;

    f32 HeightOf(const CreditsScreen::Line::Kind kind)
    {
        switch (kind) {
            case CreditsScreen::Line::Kind::Title: return TitleHeight;
            case CreditsScreen::Line::Kind::Heading: return HeadingHeight;
            default: return NameHeight;
        }
    }

    f32 ScaleOf(const CreditsScreen::Line::Kind kind)
    {
        switch (kind) {
            case CreditsScreen::Line::Kind::Title: return 1.8f;
            case CreditsScreen::Line::Kind::Heading: return 1.25f;
            default: return 1.0f;
        }
    }

    std::optional<nlohmann::json> ReadJson(const std::string& path)
    {
        std::ifstream f(path);
        if (!f) return std::nullopt;
        try {
            return nlohmann::json::parse(f);
        }
        catch (const std::exception& e) {
            LOG_WARN(UI, "CreditsScreen: '{}' is not valid JSON - {}", path, e.what());
            return std::nullopt;
        }
    }
} // namespace

std::vector<CreditsScreen::Line> CreditsScreen::Layout(const nlohmann::json& doc, f32& height)
{
    std::vector<Line> lines;
    height = 0.0f;
    if (!doc.is_object()) return lines;

    const auto add = [&](std::string text, const Line::Kind kind) {
        lines.push_back({ std::move(text), kind, height });
        height += HeightOf(kind);
    };
    if (doc.contains("title")) {
        add(doc["title"].get<std::string>(), Line::Kind::Title);
        height += SectionGap;
    }
    for (const nlohmann::json& section : doc.value("sections", nlohmann::json::array())) {
        if (section.contains("heading")) add(section["heading"].get<std::string>(), Line::Kind::Heading);
        for (const nlohmann::json& name : section.value("names", nlohmann::json::array()))
            add(name.get<std::string>(), Line::Kind::Name);
        height += SectionGap;
    }
    return lines;
}

void CreditsScreen::OnEnter()
{
    _roots.clear();
    _labels.clear();
    _scroll = 0.0f;
    _closed = false;
    Add<Scrim>();

    std::vector<Line>                   lines;
    const std::optional<nlohmann::json> doc = ReadJson(Path);
    if (doc && doc->is_object()) {
        _speed = doc->value("scroll_speed", _speed);
        lines  = Layout(*doc, _height);
    }
    if (lines.empty()) { // nothing to roll: say so, centered, until dismissed
        lines   = { { Loc::Get("ui.credits.unavailable"), Line::Kind::Name, 0.0f } };
        _height = NameHeight;
        _speed  = 0.0f;
        _scroll = (ViewHeight + NameHeight) * 0.5f;
    }

    for (const Line& line : lines) {
        auto* label                   = Add<Label>();
        label->Text                   = line.Text;
        label->Align                  = TextAlign::Center;
        label->Size                   = { LineWidth, HeightOf(line.Style) };
        label->Anchor                 = { 0.5f, 1.0f }; // the roll starts just below the screen
        label->Pivot                  = { 0.5f, 0.0f };
        label->ZOrder                 = 1;
        label->SkinOverride.FontScale = ScaleOf(line.Style);
        _labels.emplace_back(label, line.Y);
    }
    PlaceLabels();
}

void CreditsScreen::OnTick(const f32 dt, const InputManager& /*input*/)
{
    _scroll += _speed * dt;
    PlaceLabels();
    if (_speed > 0.0f && _scroll > _height + ViewHeight) Close(); // the last line has left the top
}

void CreditsScreen::PlaceLabels()
{
    for (auto& [label, y] : _labels) label->Offset = { 0.0f, y - _scroll };
}

void CreditsScreen::OnBackPressed() { Close(); }

void CreditsScreen::Close()
{
    if (_closed) return;
    _closed = true;
    if (OnClose) OnClose();
}
} // namespace Arcbit
