// Application: the player's language - which one the game starts in,
// switching it, and the fonts it needs (docs/localization.md).

#include <arcbit/app/Application.h>
#include <arcbit/app/Window.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/GameSettingsScreen.h>


namespace Arcbit
{
namespace
{
constexpr const char* LanguageKey = "engine.language"; // the player's choice, once made
} // namespace

// The engine's locale files, then the game's (and its dialogs'), and the language to start in:
// ApplicationConfig::Language, else the player's last choice, else the
// first of the OS's preferred languages the game has, else English.
void Application::StartLanguage()
{
    Loc::AddFolder("assets/engine/locale");
    Loc::AddFolder(_config.LocaleFolder);
    _dialogs.AddFolder(_config.DialogFolder); // their English is strings too: in before the fonts bake them
    LoadItems();                              // likewise
#ifdef ARCBIT_DEBUG
    Loc::AddPseudoLocale(); // "qps": find unlocalized text and tight layouts
#endif

    std::string code;
    if (_config.Language && Loc::FindLocale(_config.Language)) code = _config.Language;
    else if (const auto chosen = Settings::Get<std::string>(LanguageKey, ""); Loc::FindLocale(chosen)) code = chosen;
    else code = Loc::Match(Window::GetPreferredLanguages()).value_or(std::string(Loc::English));

    if (!Loc::SetLanguage(code)) LOG_WARN(Engine, "No locale strings for '{}'", code);
    ApplyLanguageFonts();
}

bool Application::SetLanguage(const std::string_view code)
{
    const std::string from = Loc::GetLanguage();
    if (!Loc::SetLanguage(code)) return false;
    Settings::Set<std::string>(LanguageKey, Loc::GetLanguage());
    ApplyLanguageFonts();

    EventPayload payload{ { "from", from }, { "to", Loc::GetLanguage() } };
    _events.PublishEngine(EngineEvents::LocaleChanged, std::move(payload));
    return true;
}

// Every font falls back to the languages' fonts - the active language's
// first (Chinese and Japanese draw some characters differently), then the
// others', so the language list can name each language in its own script.
// The active language's strings are baked into the UI font ahead.
void Application::ApplyLanguageFonts()
{
    std::vector<std::shared_ptr<const FontFace>> faces;
    const auto add = [&](const std::vector<std::string>& paths) {
        for (const std::string& path : paths) {
            auto& face = _languageFaces[path];
            if (!face) face = FontFace::Load(path);
            if (face && std::ranges::find(faces, face) == faces.end()) faces.push_back(face);
        }
    };
    if (const Loc::Locale* active = Loc::FindLocale(Loc::GetLanguage())) add(active->Fonts);
    for (const Loc::Locale& locale : Loc::GetLocales()) add(locale.Fonts);
    FontAtlas::SetSharedFallbacks(std::move(faces));

    Loc::ForEachString([this](const std::string_view text) { _debugFont.Prepare(text); });
}

void Application::ShowGameSettingsScreen()
{
    auto screen        = std::make_unique<GameSettingsScreen>();
    screen->OnLanguage = [this](const std::string& code) { SetLanguage(code); };
    screen->OnBack     = [this, ptr = screen.get()] { _ui.Close(ptr); };
    screen->OnBuilt    = [this](GameSettingsScreen& built) { OnGameSettings(built); };
    _ui.Push(std::move(screen));
}
} // namespace Arcbit
