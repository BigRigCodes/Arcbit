#include "DemoSelectScreen.h"

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/Widgets.h>

#include <format>

using namespace Arcbit;

namespace
{
constexpr ActionID ActionDemoMenu = MakeAction("Demo_Menu");

constexpr f32   PanelW      = 720.0f;
constexpr f32   ButtonW     = 640.0f;
constexpr f32   ButtonH     = 44.0f;
constexpr f32   ButtonGap   = 52.0f;
constexpr f32   ListTop     = 104.0f;
constexpr Color CurrentText = { 0.95f, 0.80f, 0.30f, 1.0f };
} // namespace

void DemoSelectScreen::OnEnter()
{
    _roots.clear();
    Add<Scrim>();

    const auto entries = Demo::Entries();
    auto*      bg      = Add<Panel>();
    bg->Size   = { PanelW, ListTop + static_cast<f32>(entries.size()) * ButtonGap + 24.0f };
    bg->Anchor = { 0.5f, 0.5f };
    bg->Pivot  = { 0.5f, 0.5f };
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = "Arcbit Demos";
    title->Align  = TextAlign::Center;
    title->Size   = { PanelW, 32.0f };
    title->Anchor = { 0.5f, 0.0f };
    title->Pivot  = { 0.5f, 0.0f };
    title->Offset = { 0.0f, 20.0f };
    title->ZOrder = 2;

    auto* hint   = bg->AddChild<Label>();
    hint->Text   = "PgUp / PgDn cycle demos   -   F1 or Esc closes";
    hint->Align  = TextAlign::Center;
    hint->Size   = { PanelW, 24.0f };
    hint->Anchor = { 0.5f, 0.0f };
    hint->Pivot  = { 0.5f, 0.0f };
    hint->Offset = { 0.0f, 60.0f };
    hint->ZOrder = 2;

    for (size_t i = 0; i < entries.size(); ++i) {
        auto* btn      = bg->AddChild<Button>();
        btn->Text      = std::format("{}  -  {}", entries[i].Name, entries[i].Summary);
        btn->Size      = { ButtonW, ButtonH };
        btn->Anchor    = { 0.5f, 0.0f };
        btn->Pivot     = { 0.5f, 0.0f };
        btn->Offset    = { 0.0f, ListTop + static_cast<f32>(i) * ButtonGap };
        btn->Focusable = true;
        btn->ZOrder    = 2;
        if (i == Current) btn->SkinOverride.TextLabel = CurrentText;
        btn->OnClick = [this, i] { if (OnPick) OnPick(i); };
    }
}

void DemoSelectScreen::OnBackPressed()
{
    if (OnClose) OnClose();
}

void DemoSelectScreen::OnTick(f32 /*dt*/, const InputManager& input)
{
    if (input.JustPressed(ActionDemoMenu) && OnClose) OnClose(); // F1 toggles
}
