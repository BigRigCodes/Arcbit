#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// LanguageScreen - every language the game has (Loc::GetLocales), each named
// in itself ("Francais" whatever the current language), the current one
// marked. Picking one switches at once (OnSelect: Application::SetLanguage)
// and the screen stays, now in that language, until Back.
//
// Layout: assets/engine/ui/language.arcui - a panel "bg" with a "title"
// and a "back-btn"; the language buttons are added between them.
// ---------------------------------------------------------------------------
class LanguageScreen : public UIScreen
{
public:
    static constexpr const char* LayoutPath = "assets/engine/ui/language.arcui";

    std::function<void(const std::string& code)> OnSelect;
    std::function<void()>                        OnBack;

    LanguageScreen() { BlocksInput = true; }

    void OnEnter() override;
    void OnBackPressed() override { if (OnBack) OnBack(); }

protected:
    void OnLocaleChanged() override; // mark the new current language, focus kept

private:
    void BuildFallback();
    void AddLanguageButtons();
};
} // namespace Arcbit
