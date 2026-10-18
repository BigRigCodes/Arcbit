#include <arcbit/ui/LanguageScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

namespace Arcbit
{
namespace
{
constexpr f32   ButtonW = 260.0f, ButtonH = 44.0f, Gap = 52.0f, StartY = 72.0f, Bottom = 24.0f;
constexpr Color Current = { 0.45f, 0.75f, 1.0f, 1.0f }; // the language in use
} // namespace

void LanguageScreen::OnEnter()
{
    _roots.clear();
    if (!LoadLayout(LayoutPath)) {
        LOG_WARN(UI, "LanguageScreen: '{}' not found, using built-in layout", LayoutPath);
        BuildFallback();
    }
    if (auto* back = FindWidget<Button>("back-btn")) back->OnClick = [this] { if (OnBack) OnBack(); };
    AddLanguageButtons();
}

void LanguageScreen::OnLocaleChanged()
{
    const i32 focus = GetFocusIndex();
    OnEnter();
    FocusAt(focus);
}

// One button per language, in the panel above Back; the panel grows to fit.
void LanguageScreen::AddLanguageButtons()
{
    UIWidget* bg   = FindWidget<UIWidget>("bg");
    auto*     back = FindWidget<Button>("back-btn");
    if (!bg) return;

    f32         y       = StartY;
    i32         current = 0, index = 0;
    const auto& active  = Loc::GetLanguage();
    for (const Loc::Locale& locale : Loc::GetLocales()) {
        auto* btn      = bg->AddChild<Button>();
        btn->Name      = "lang-" + locale.Code;
        btn->Text      = locale.Name;
        btn->Size      = { ButtonW, ButtonH };
        btn->Anchor    = { 0.5f, 0.0f };
        btn->Pivot     = { 0.5f, 0.0f };
        btn->Offset    = { 0.0f, y };
        btn->ZOrder    = 2;
        btn->TabOrder  = static_cast<u32>(index);
        btn->OnClick   = [this, code = locale.Code] { if (OnSelect) OnSelect(code); };
        if (locale.Code == active) {
            btn->SkinOverride.TextLabel = Current;
            current                     = index;
        }
        y += Gap;
        ++index;
    }
    if (back) {
        back->Offset.Y = y + Gap * 0.25f;
        back->TabOrder = static_cast<u32>(index);
        bg->Size.Y     = back->Offset.Y + back->Size.Y + Bottom;
    }
    FocusAt(current);
}

void LanguageScreen::BuildFallback()
{
    Add<Scrim>();
    auto* bg   = Add<Panel>();
    bg->Name   = "bg";
    bg->Size   = { 340.0f, 200.0f };
    bg->Anchor = bg->Pivot = { 0.5f, 0.5f };
    bg->ZOrder = 1;

    auto* title = bg->AddChild<Label>();
    title->Name = "title";
    title->SetTextKey("ui.language.title");
    title->Align  = TextAlign::Center;
    title->Size   = { 340.0f, 32.0f };
    title->Anchor = title->Pivot = { 0.5f, 0.0f };
    title->Offset = { 0.0f, 20.0f };
    title->ZOrder = 2;

    auto* back = bg->AddChild<Button>();
    back->Name = "back-btn";
    back->SetTextKey("ui.common.back");
    back->Size   = { 160.0f, 40.0f };
    back->Anchor = back->Pivot = { 0.5f, 0.0f };
    back->ZOrder = 2;
}
} // namespace Arcbit
