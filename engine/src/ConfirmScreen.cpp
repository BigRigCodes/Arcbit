#include <arcbit/ui/ConfirmScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/ui/Widgets.h>

namespace Arcbit
{
void ConfirmScreen::OnEnter()
{
    _roots.clear();
    constexpr f32 PanelW  = 440.0f;
    constexpr f32 PanelH  = 170.0f;
    constexpr f32 ButtonW = 150.0f;

    Add<Scrim>();
    auto* bg   = Add<Panel>();
    bg->Size   = { PanelW, PanelH };
    bg->Anchor = { 0.5f, 0.5f };
    bg->Pivot  = { 0.5f, 0.5f };
    bg->ZOrder = 1;

    auto* message     = bg->AddChild<Label>();
    message->Text     = Message;
    message->Align    = TextAlign::Center;
    message->WordWrap = true;
    message->Size     = { PanelW - 40.0f, 70.0f };
    message->Anchor   = { 0.5f, 0.0f };
    message->Pivot    = { 0.5f, 0.0f };
    message->Offset   = { 0.0f, 28.0f };
    message->ZOrder   = 2;

    const auto button = [&](const std::string& text, const f32 x, std::function<void()> onClick) {
        auto* b    = bg->AddChild<Button>();
        b->Text    = text;
        b->Size    = { ButtonW, 44.0f };
        b->Anchor  = { 0.5f, 1.0f };
        b->Pivot   = { 0.5f, 1.0f };
        b->Offset  = { x, -20.0f };
        b->ZOrder  = 2;
        b->OnClick = std::move(onClick);
    };
    button(YesText.empty() ? Loc::Get("ui.common.yes") : YesText, -ButtonW * 0.5f - 10.0f, OnYes);
    button(NoText.empty() ? Loc::Get("ui.common.no") : NoText, ButtonW * 0.5f + 10.0f, OnNo);
    FocusAt(1); // No
}
} // namespace Arcbit
