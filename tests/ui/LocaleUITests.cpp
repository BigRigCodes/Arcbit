// UI that follows the language: widgets' text keys, screens told the
// language changed, and the Language screen.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/LanguageScreen.h>
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

TEST_CASE("The Language screen lists each language by its own name and picks one", "[ui][loc]")
{
    Languages    languages;
    UIManager    ui;
    InputManager input;

    std::vector<std::string> picked;
    auto                     screen = std::make_unique<LanguageScreen>();
    screen->OnSelect                = [&](const std::string& code) { picked.push_back(code); };
    auto* raw                       = screen.get();
    ui.Push(std::move(screen));
    Tick(ui, input);

    auto* en = raw->FindWidget<Button>("lang-en");
    auto* fr = raw->FindWidget<Button>("lang-fr");
    REQUIRE(en);
    REQUIRE(fr);
    CHECK(en->Text == "English");
    CHECK(fr->Text == "Francais");
    CHECK(raw->GetFocusedWidget() == en); // the current language
    CHECK(en->SkinOverride.TextLabel.has_value());
    CHECK_FALSE(fr->SkinOverride.TextLabel.has_value());

    fr->OnClick();
    CHECK(picked == std::vector<std::string>{ "fr" });
}
