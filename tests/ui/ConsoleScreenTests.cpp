// The console's window driven like a player would: type, press Enter, type
// again - through a headless UIManager with the UI's keys bound by hand.

#include <arcbit/app/DebugConsole.h>
#include <arcbit/core/Clipboard.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/ui/ConsoleScreen.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/UIManager.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Arcbit;

namespace
{
struct Fixture
{
    InputManager input;
    UIManager    ui;
    DebugConsole console;
    TextInput*   line = nullptr;

    Fixture()
    {
        input.RegisterAction(UIManager::ActionConfirm, "UI_Confirm");
        input.BindKey(UIManager::ActionConfirm, Key::Enter);
        input.RegisterAction(UIManager::ActionTextBackspace, "UI_TextBackspace");
        input.BindKey(UIManager::ActionTextBackspace, Key::Backspace);
        input.RegisterAction(ConsoleScreen::ActionHistoryUp, "Console_HistoryUp");
        input.BindKey(ConsoleScreen::ActionHistoryUp, Key::Up);
        input.RegisterAction(UIManager::ActionCtrlMod, "UI_CtrlMod");
        input.BindKey(UIManager::ActionCtrlMod, Key::LeftCtrl);
        input.RegisterAction(UIManager::ActionTextPaste, "UI_TextPaste");
        input.BindKey(UIManager::ActionTextPaste, Key::V);

        auto screen     = std::make_unique<ConsoleScreen>();
        screen->Console = &console;
        ui.Push(std::move(screen));
        Tick(); // the frame it opened on takes no input
        line = ui.Top()->FindWidget<TextInput>("console-line");
    }
    void Tick() { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
    void Type(const char* text)
    {
        input.InjectTextInput(text);
        input.ProcessEdges();
        Tick();
    }
    void Press(const SDL_Scancode key)
    {
        input.InjectKeyEvent(key, true);
        input.ProcessEdges();
        Tick();
        input.InjectKeyEvent(key, false);
        input.ProcessEdges();
        Tick();
    }
};
} // namespace

TEST_CASE("Typing a command and pressing Enter runs it; typing again starts a fresh line", "[console][ui]")
{
    Fixture f;
    REQUIRE(f.line);
    f.Type("help");
    CHECK(f.line->Text == "help");
    f.Press(SDL_SCANCODE_RETURN);
    CHECK(f.console.GetHistory() == std::vector<std::string>{ "help" });
    CHECK(f.line->Text.empty());

    f.Type("h"); // after the line was cleared under it
    CHECK(f.line->Text == "h");
    f.Press(SDL_SCANCODE_BACKSPACE);
    CHECK(f.line->Text.empty());
}

TEST_CASE("Up recalls the last line, ready to edit at its end", "[console][ui]")
{
    Fixture f;
    f.Type("help");
    f.Press(SDL_SCANCODE_RETURN);
    f.Press(SDL_SCANCODE_UP);
    CHECK(f.line->Text == "help");
    f.Type(" clear");
    CHECK(f.line->Text == "help clear");
}

TEST_CASE("The line shows a suggestion that Tab takes", "[console][ui]")
{
    Fixture f;
    f.input.RegisterAction(ConsoleScreen::ActionComplete, "Console_Complete");
    f.input.BindKey(ConsoleScreen::ActionComplete, Key::Tab);
    f.Type("he");
    CHECK(f.line->Suggestion == "lp");
    f.Press(SDL_SCANCODE_TAB);
    CHECK(f.line->Text == "help");
    CHECK(f.line->Suggestion.empty());
}

// Ctrl itself is read from the keyboard's live state (InputManager::Update),
// which a test can't hold down; the Ctrl keys are tested on TextInput itself.
TEST_CASE("V alone doesn't paste into the console's line; copy takes the output", "[console][ui][clipboard]")
{
    Fixture f;
    Clipboard::SetText("give potion 3");
    f.Press(SDL_SCANCODE_V); // without Ctrl it's typing, not a paste
    CHECK(f.line->Text.empty());

    f.console.Print("first");
    f.console.Print("second");
    f.console.Execute("copy 2");
    CHECK(Clipboard::GetText() == "first\nsecond\n"); // not the "> copy 2" it echoed
    CHECK(f.console.GetLines().back() == "copied 2 lines");
}
