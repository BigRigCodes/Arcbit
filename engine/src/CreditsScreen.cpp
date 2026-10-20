#include <arcbit/ui/CreditsScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/ui/Widgets.h>

#include <nlohmann/json.hpp>

#include <cmath>
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
    constexpr f32 ImageHeight   = 96.0f; // a logo's, unless it says
    constexpr f32 ImagePad      = 16.0f; // below a logo line
    constexpr f32 ImageGap      = 32.0f; // between the logos of a row

    f32 HeightOf(const CreditsScreen::Line& line)
    {
        switch (line.Style) {
            case CreditsScreen::Line::Kind::Title: return TitleHeight;
            case CreditsScreen::Line::Kind::Heading: return HeadingHeight;
            case CreditsScreen::Line::Kind::Images: return line.ImageHeight + ImagePad;
            default: return NameHeight;
        }
    }

    // { "image": path } or { "images": [paths] }, with "height" and "uv".
    CreditsScreen::Line ReadImages(const nlohmann::json& entry)
    {
        CreditsScreen::Line line{ .Style = CreditsScreen::Line::Kind::Images };
        if (const auto it = entry.find("image"); it != entry.end() && it->is_string()) line.Images.push_back(*it);
        for (const nlohmann::json& path : entry.value("images", nlohmann::json::array()))
            if (path.is_string()) line.Images.push_back(path);
        line.ImageHeight = entry.value("height", ImageHeight);
        if (const auto it = entry.find("uv"); it != entry.end() && it->is_array() && it->size() == 4)
            line.UV = { (*it)[0].get<f32>(), (*it)[1].get<f32>(), (*it)[2].get<f32>(), (*it)[3].get<f32>() };
        return line;
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

    const auto push = [&](Line line) {
        line.Y = height;
        height += HeightOf(line);
        lines.push_back(std::move(line));
    };
    const auto add = [&](const std::string& text, const Line::Kind kind) { // any line may be a key
        push({ .Text = Loc::Get(text), .Style = kind });
    };
    if (doc.contains("title")) {
        add(doc["title"].get<std::string>(), Line::Kind::Title);
        height += SectionGap;
    }
    for (const nlohmann::json& section : doc.value("sections", nlohmann::json::array())) {
        if (section.contains("heading")) add(section["heading"].get<std::string>(), Line::Kind::Heading);
        for (const nlohmann::json& name : section.value("names", nlohmann::json::array())) {
            if (name.is_string()) add(name.get<std::string>(), Line::Kind::Name);
            else if (name.is_object()) push(ReadImages(name));
        }
        height += SectionGap;
    }
    return lines;
}

void CreditsScreen::OnEnter()
{
    _roots.clear();
    _placed.clear();
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
        lines   = { { .Text = Loc::Get("ui.credits.unavailable") } };
        _height = NameHeight;
        _speed  = 0.0f;
        _scroll = (ViewHeight + NameHeight) * 0.5f;
    }

    for (const Line& line : lines) {
        if (line.Style == Line::Kind::Images) AddImages(line);
        else AddLabel(line);
    }
    Place();
}

void CreditsScreen::AddLabel(const Line& line)
{
    auto* label                   = Add<Label>();
    label->Text                   = line.Text;
    label->Align                  = TextAlign::Center;
    label->Size                   = { LineWidth, HeightOf(line) };
    label->Anchor                 = { 0.5f, 1.0f }; // the roll starts just below the screen
    label->Pivot                  = { 0.5f, 0.0f };
    label->ZOrder                 = 1;
    label->SkinOverride.FontScale = ScaleOf(line.Style);
    _placed.push_back({ label, { 0.0f, line.Y } });
}

// Each logo at the line's height and its own aspect (of the part shown),
// the row centered. A logo is named by its path.
void CreditsScreen::AddImages(const Line& line)
{
    struct Logo { const std::string* Path; TextureHandle Texture; f32 Width; };
    std::vector<Logo> logos;
    for (const std::string& path : line.Images) {
        const LoadedImage image = LoadImage ? LoadImage(path) : LoadedImage{};
        if (!image.Texture.IsValid() || image.Size.Y <= 0.0f) {
            LOG_WARN(UI, "CreditsScreen: logo '{}' not found - left out", path);
            continue;
        }
        const f32 aspect = image.Size.X * std::abs(line.UV.U1 - line.UV.U0) / (image.Size.Y * std::abs(line.UV.V1 - line.UV.V0));
        logos.push_back({ &path, image.Texture, line.ImageHeight * aspect });
    }
    f32 width = ImageGap * static_cast<f32>(logos.empty() ? 0 : logos.size() - 1);
    for (const Logo& logo : logos) width += logo.Width;
    f32 x = -width * 0.5f;
    for (const Logo& logo : logos) {
        auto* image    = Add<Image>();
        image->Name    = *logo.Path;
        image->Texture = logo.Texture;
        image->Sampler = ImageSampler;
        image->UV      = line.UV;
        image->Size    = { logo.Width, line.ImageHeight };
        image->Anchor  = { 0.5f, 1.0f };
        image->Pivot   = { 0.5f, 0.0f };
        image->ZOrder  = 1;
        _placed.push_back({ image, { x + logo.Width * 0.5f, line.Y } });
        x += logo.Width + ImageGap;
    }
}

void CreditsScreen::OnTick(const f32 dt, const InputManager& /*input*/)
{
    _scroll += _speed * dt;
    Place();
    if (_speed > 0.0f && _scroll > _height + ViewHeight) Close(); // the last line has left the top
}

void CreditsScreen::Place()
{
    for (const auto& [widget, at] : _placed) widget->Offset = { at.X, at.Y - _scroll };
}

void CreditsScreen::OnBackPressed() { Close(); }

void CreditsScreen::Close()
{
    if (_closed) return;
    _closed = true;
    if (OnClose) OnClose();
}
} // namespace Arcbit
