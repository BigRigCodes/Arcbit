#include <arcbit/ui/GameSettingsScreen.h>
#include <arcbit/ui/DialogScreen.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

namespace Arcbit
{
void GameSettingsScreen::OnEnter()
{
    _roots.clear();
    if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "GameSettingsScreen: '{}' not found", LayoutPath);
    WireLanguage();
    WireDialog();
    if (auto* back = FindWidget<Button>("back-btn")) back->OnClick = [this] { if (OnBack) OnBack(); };
    if (OnBuilt) OnBuilt(*this);
}

void GameSettingsScreen::OnBackPressed()
{
    if (OnBack) OnBack();
}

// Names, not keys: each language reads its own name in any language. Its
// code is a search term, so "fr" finds Francais in a long list.
void GameSettingsScreen::WireLanguage()
{
    auto* dropdown = FindWidget<Dropdown>("language-dd");
    if (!dropdown) return;
    const std::vector<Loc::Locale> locales = Loc::GetLocales();
    dropdown->Items.clear();
    dropdown->SearchTerms.clear();
    for (const Loc::Locale& locale : locales) {
        if (locale.Code == Loc::GetLanguage()) dropdown->SelectedIndex = static_cast<i32>(dropdown->Items.size());
        dropdown->Items.push_back(locale.Name);
        dropdown->SearchTerms.push_back(locale.Code);
    }
    dropdown->OnChanged = [this, locales](const i32 index) {
        if (OnLanguage && index >= 0 && index < static_cast<i32>(locales.size()))
            OnLanguage(locales[static_cast<usize>(index)].Code);
    };
}
void GameSettingsScreen::WireDialog()
{
    using namespace DialogSettings;
    if (auto* speed = FindWidget<Dropdown>("text-speed-dd")) {
        speed->ItemKeys = { "ui.game.speed.slow", "ui.game.speed.normal", "ui.game.speed.fast", "ui.game.speed.instant" };
        speed->Items.clear();
        for (const std::string& key : speed->ItemKeys) speed->Items.push_back(Loc::Get(key));
        speed->SelectedIndex = static_cast<i32>(GetTextSpeed());
        speed->OnChanged     = [](const i32 index) {
            if (index >= 0 && index < static_cast<i32>(TextSpeedNames.size())) SetTextSpeed(static_cast<TextSpeed>(index));
        };
    }
    if (auto* autoAdvance = FindWidget<Switch>("auto-advance")) {
        autoAdvance->On        = GetAutoAdvance();
        autoAdvance->OnChanged = [](const bool on) { SetAutoAdvance(on); };
    }
}
} // namespace Arcbit
