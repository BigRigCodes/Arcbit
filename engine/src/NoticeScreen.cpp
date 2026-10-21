#include <arcbit/ui/NoticeScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <cmath>

namespace Arcbit
{
namespace
{
constexpr f32 RowHeight = 32.0f;
constexpr f32 RowChars  = 52.0f; // about what fits a row at the card's width: a long line wraps

std::string Join(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines) text += (text.empty() ? "" : "\n") + line;
    return text;
}

// Rows the lines take, wrapped (an estimate: no font at hand).
f32 Rows(const std::vector<std::string>& lines)
{
    f32 rows = 0.0f;
    for (const std::string& line : lines) rows += std::max(1.0f, std::ceil(static_cast<f32>(line.size()) / RowChars));
    return rows;
}

Label* AddText(UIWidget& parent, const std::string& text, const f32 width, const f32 height, const f32 y, const f32 scale)
{
    auto* label                   = parent.AddChild<Label>();
    label->Text                   = text;
    label->Align                  = TextAlign::Center;
    label->WordWrap               = true;
    label->Size                   = { width, height };
    label->Anchor                 = { 0.5f, 0.0f };
    label->Pivot                  = { 0.5f, 0.0f };
    label->Offset                 = { 0.0f, y };
    label->ZOrder                 = parent.ZOrder + 1;
    label->SkinOverride.FontScale = scale;
    return label;
}
} // namespace

void NoticeScreen::OnEnter()
{
    _roots.clear();
    constexpr f32 PanelW = 720.0f;
    const f32     body   = Rows(Lines) * RowHeight;
    const f32     panelH = 28.0f + 44.0f + body + 24.0f + 44.0f + 24.0f; // title, lines, OK

    Add<Scrim>();
    auto* bg   = Add<Panel>();
    bg->Size   = { PanelW, panelH };
    bg->Anchor = { 0.5f, 0.5f };
    bg->Pivot  = { 0.5f, 0.5f };
    bg->ZOrder = 1;
    bg->SkinOverride.PanelBg = Color{ 0.2f, 0.2f, 0.24f, 0.98f }; // solid: it may sit over another menu
    AddText(*bg, Title, PanelW - 40.0f, 40.0f, 28.0f, 1.25f);
    AddText(*bg, Join(Lines), PanelW - 60.0f, body, 72.0f, 1.0f);

    auto* ok    = bg->AddChild<Button>();
    ok->Text    = Loc::Get("ui.common.ok");
    ok->Size    = { 150.0f, 44.0f };
    ok->Anchor  = { 0.5f, 1.0f };
    ok->Pivot   = { 0.5f, 1.0f };
    ok->Offset  = { 0.0f, -24.0f };
    ok->ZOrder  = 2;
    ok->OnClick = [this] { if (OnOk) OnOk(); };
    FocusAt(0);
}

void NoticeToast::OnEnter()
{
    _roots.clear();
    _age              = 0.0f;
    _done             = false;
    constexpr f32 BoxW = 760.0f;
    const f32     body = Rows(Lines) * RowHeight * 0.8f;

    auto* box                 = Add<Panel>();
    box->Size                 = { BoxW, 16.0f + 32.0f + body + 16.0f };
    box->Anchor               = { 0.5f, 1.0f };
    box->Pivot                = { 0.5f, 1.0f };
    box->Offset               = { 0.0f, -40.0f };
    box->SkinOverride.PanelBg = Color{ 0.08f, 0.09f, 0.12f, 0.88f };
    AddText(*box, Title, BoxW - 32.0f, 30.0f, 14.0f, 0.95f);
    AddText(*box, Join(Lines), BoxW - 32.0f, body, 48.0f, 0.8f);
    _box = box;
}

void NoticeToast::OnUpdate(const f32 dt)
{
    _age += dt;
    if (_box) _box->Opacity = std::clamp(Seconds - _age, 0.0f, 1.0f);
    if (_age < Seconds || _done) return;
    _done = true;
    if (OnDone) OnDone();
}
} // namespace Arcbit
