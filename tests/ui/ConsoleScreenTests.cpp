// The console's window driven like a player would: type, press Enter, type
// again - through a headless UIManager with the UI's keys bound by hand.

#include <arcbit/app/DebugConsole.h>
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
