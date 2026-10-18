// Dropdown: keyboard / gamepad use through UIManager, Back closing just the
// list, and filtering a long list by typing.

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_scancode.h>

#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
struct BackCounter : UIScreen
{
    int Backs = 0;
    void OnBackPressed() override { ++Backs; }
};

struct Fixture
{
    InputManager input;
    UIManager    ui;
    BackCounter* screen   = nullptr;
    Dropdown*    dropdown = nullptr;
    std::vector<i32> changes;

    explicit Fixture(const std::vector<std::string>& items)
    {
        input.RegisterAction(UIManager::ActionConfirm, "UI_Confirm");
        input.BindKey(UIManager::ActionConfirm, Key::Enter);
        input.RegisterAction(UIManager::ActionFocusNext, "UI_FocusNext");
        input.BindKey(UIManager::ActionFocusNext, Key::Down);
        input.RegisterAction(UIManager::ActionFocusPrev, "UI_FocusPrev");
        input.BindKey(UIManager::ActionFocusPrev, Key::Up);
        input.RegisterAction(UIManager::ActionTextEscape, "UI_TextEscape");
        input.BindKey(UIManager::ActionTextEscape, Key::Escape);
        input.RegisterAction(UIManager::ActionTextBackspace, "UI_TextBackspace");
        input.BindKey(UIManager::ActionTextBackspace, Key::Backspace);

        auto owned          = std::make_unique<BackCounter>();
        dropdown            = owned->Add<Dropdown>();
        dropdown->Items     = items;
        dropdown->Size      = { 200.0f, 30.0f };
        dropdown->OnChanged = [this](const i32 index) { changes.push_back(index); };
        owned->Add<Button>(); // something to move focus to
        screen = owned.get();
        ui.Push(std::move(owned));
        Tick();
        screen->FocusAt(0);
    }
    void Tick() { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
    void Press(const SDL_Scancode key)
    {
        input.InjectKeyEvent(key, true);
        input.ProcessEdges();
        Tick();
        input.InjectKeyEvent(key, false);
        input.ProcessEdges();
        Tick();
    }
    void Type(const char* text)
    {
        input.InjectTextInput(text);
        input.ProcessEdges();
        Tick();
    }
};
} // namespace

TEST_CASE("A dropdown opens, moves and picks with the keys", "[ui][dropdown]")
{
    Fixture f({ "Easy", "Normal", "Hard" });
    f.dropdown->SelectedIndex = 0;

    f.Press(SDL_SCANCODE_RETURN);
    REQUIRE(f.dropdown->IsOpen());
    f.Press(SDL_SCANCODE_DOWN); // moves the highlight, not the focus
    f.Press(SDL_SCANCODE_DOWN);
    f.Press(SDL_SCANCODE_DOWN); // stops at the end
    CHECK(f.screen->GetFocusedWidget() == f.dropdown);
    f.Press(SDL_SCANCODE_RETURN);
    CHECK_FALSE(f.dropdown->IsOpen());
    CHECK(f.dropdown->SelectedIndex == 2);
    CHECK(f.changes == std::vector<i32>{ 2 });

    f.Press(SDL_SCANCODE_DOWN); // closed again: the arrows move focus
    CHECK(f.screen->GetFocusedWidget() != f.dropdown);
}

TEST_CASE("Back closes an open dropdown and nothing more", "[ui][dropdown]")
{
    Fixture f({ "A", "B" });
    f.Press(SDL_SCANCODE_RETURN);
    REQUIRE(f.dropdown->IsOpen());
    f.Press(SDL_SCANCODE_ESCAPE);
    CHECK_FALSE(f.dropdown->IsOpen());
    CHECK(f.screen->GetFocusedWidget() == f.dropdown);
    CHECK(f.screen->Backs == 0);
    CHECK(f.changes.empty());

    f.Press(SDL_SCANCODE_ESCAPE); // closed: the screen's own Back
    CHECK(f.screen->Backs == 1);
}

TEST_CASE("Typing filters a long list by item or search term", "[ui][dropdown]")
{
    Fixture f({ "English", "Francais", "Deutsch", "Espanol", "Italiano", "Nederlands", "Polski", "Portugues", "Svenska" });
    f.dropdown->SearchTerms = { "en", "fr", "de", "es", "it", "nl", "pl", "pt", "sv" };
    REQUIRE(f.dropdown->Filters());

    f.Press(SDL_SCANCODE_RETURN);
    CHECK(f.dropdown->WantsTextInput());
    f.Type("PO");
    CHECK(f.dropdown->GetShown() == std::vector<i32>{ 6, 7 }); // Polski, Portugues: case ignored
    f.Press(SDL_SCANCODE_BACKSPACE);
    CHECK(f.dropdown->GetFilter() == "P");
    f.Press(SDL_SCANCODE_BACKSPACE);
    f.Type("fr"); // the code finds it
    CHECK(f.dropdown->GetShown() == std::vector<i32>{ 1 });
    f.Press(SDL_SCANCODE_RETURN);
    CHECK(f.dropdown->SelectedIndex == 1);
    CHECK_FALSE(f.dropdown->WantsTextInput());
}

TEST_CASE("A short list doesn't filter or take typing", "[ui][dropdown]")
{
    Fixture f({ "A", "B", "C" });
    f.Press(SDL_SCANCODE_RETURN);
    CHECK_FALSE(f.dropdown->WantsTextInput());
    f.Type("b");
    CHECK(f.dropdown->GetShown().size() == 3);
}
