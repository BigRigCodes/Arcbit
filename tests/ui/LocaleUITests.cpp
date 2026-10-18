// UI that follows the language: widgets' text keys, screens told the
// language changed, and the Game settings screen.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/GameSettingsScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
// English and French, with names, in a temp locale folder; cleared after.
struct Languages
{
    ArcbitTests::TempDir dir;

    Languages()
    {
        Loc::Clear();
        std::ofstream(dir.File("en.json")) << R"({ "$locale": { "name": "English" }, "hi": "Hello", "ok": "OK" })";
        std::ofstream(dir.File("fr.json")) << R"({ "$locale": { "name": "Francais" }, "hi": "Bonjour", "ok": "D'accord" })";
        Loc::AddFolder(dir.File(""));
        REQUIRE(Loc::SetLanguage("en"));
    }
    ~Languages() { Loc::Clear(); }
};

struct CountingScreen : UIScreen
{
    int Changes = 0;
    void OnLocaleChanged() override { ++Changes; }
};

void Tick(UIManager& ui, const InputManager& input) { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
} // namespace

TEST_CASE("Widgets with text keys follow the language; screens hear about it once", "[ui][loc]")
{
    Languages    languages;
    UIManager    ui;
    InputManager input;

    auto  screen = std::make_unique<CountingScreen>();
    auto* label  = screen->Add<Label>();
    auto* button = screen->Add<Button>();
    auto* box    = screen->Add<Checkbox>();
    auto* plain  = screen->Add<Label>();
    label->SetTextKey("hi");
    button->SetTextKey("ok");
    box->SetLabelKey("hi");
    plain->Text   = "as written";
    auto* counter = screen.get();
    ui.Push(std::move(screen));
    Tick(ui, input);
    CHECK(label->Text == "Hello");
    CHECK(counter->Changes == 0); // nothing changed since it was built

    REQUIRE(Loc::SetLanguage("fr"));
    Tick(ui, input);
    Tick(ui, input);
    CHECK(label->Text == "Bonjour");
    CHECK(button->Text == "D'accord");
    CHECK(box->Label == "Bonjour");
    CHECK(plain->Text == "as written"); // no key: left alone
    CHECK(counter->Changes == 1);
}

TEST_CASE("Loc::Match takes the first preferred language the game has", "[ui][loc]")
{
    Languages languages;
    using List = std::vector<std::string>;
    CHECK(Loc::Match(List{ "de-DE", "fr-CA", "en-US" }) == "fr"); // by language
    CHECK(Loc::Match(List{ "EN_gb" }) == "en");
    CHECK_FALSE(Loc::Match(List{ "de", "ja" }));
    CHECK_FALSE(Loc::Match(List{}));
}

TEST_CASE("The Game settings screen offers every language by its own name", "[ui][loc]")
{
    Languages    languages;
    UIManager    ui;
    InputManager input;

    std::vector<std::string> picked;
    int                      built  = 0;
    auto                     screen = std::make_unique<GameSettingsScreen>();
    screen->LayoutPath              = ArcbitTests::SourcePath("engine/assets/ui/game_settings.arcui").string();
    screen->OnLanguage              = [&](const std::string& code) { picked.push_back(code); };
    screen->OnBuilt                 = [&](GameSettingsScreen&) { ++built; };
    auto* raw                       = screen.get();
    ui.Push(std::move(screen));
    Tick(ui, input);

    auto* dropdown = raw->FindWidget<Dropdown>("language-dd");
    REQUIRE(dropdown);
    CHECK(dropdown->Items == std::vector<std::string>{ "English", "Francais" });
    CHECK(dropdown->SearchTerms == std::vector<std::string>{ "en", "fr" });
    CHECK(dropdown->SelectedIndex == 0); // the current language
    CHECK(built == 1);

    dropdown->OnChanged(1);
    CHECK(picked == std::vector<std::string>{ "fr" });
}
