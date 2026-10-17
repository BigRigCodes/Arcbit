// UIManager's screen stack, without a renderer: Push / Pop ordering and the
// StaysOnTop rule.

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace Arcbit;

namespace
{
std::unique_ptr<UIScreen> MakeScreen(const bool staysOnTop = false)
{
    auto screen             = std::make_unique<UIScreen>();
    screen->TransitionSpeed = 0.0f; // Pop removes at once
    screen->StaysOnTop      = staysOnTop;
    return screen;
}
} // namespace

TEST_CASE("Screens pushed under a StaysOnTop screen go beneath it", "[ui]")
{
    // Regression: the first scene loads under the splash now, and a HUD it
    // pushed landed on top - the splash stopped ticking and never finished.
    UIManager ui;
    auto      splash    = MakeScreen(true);
    UIScreen* splashPtr = splash.get();
    ui.Push(std::move(splash));
    ui.Push(MakeScreen());
    CHECK(ui.Top() == splashPtr);

    auto      menu    = MakeScreen();
    UIScreen* menuPtr = menu.get();
    ui.Push(std::move(menu));
    CHECK(ui.Top() == splashPtr);

    ui.Pop(); // closes the topmost ordinary screen, not the splash
    CHECK(ui.Top() == splashPtr);
    ui.Remove(splashPtr);
    CHECK(ui.Top() != menuPtr); // the menu was the one popped
    CHECK(ui.Top() != nullptr); // the first ordinary screen remains
}

TEST_CASE("Pop with only StaysOnTop screens does nothing", "[ui]")
{
    UIManager ui;
    auto      splash    = MakeScreen(true);
    UIScreen* splashPtr = splash.get();
    ui.Push(std::move(splash));
    ui.Pop();
    CHECK(ui.Top() == splashPtr);
}

TEST_CASE("A screen can remove itself from its own tick", "[ui]")
{
    // Regression: the splash removed itself in OnComplete (called from its
    // OnTick) and UIManager::Update kept using the destroyed screen.
    struct SelfRemoving : UIScreen
    {
        UIManager* Ui = nullptr;
        void       OnTick(f32, const InputManager&) override { Ui->Remove(this); }
    };
    UIManager    ui;
    InputManager input;
    auto         screen = std::make_unique<SelfRemoving>();
    screen->Ui          = &ui;
    ui.Push(std::move(screen));

    ui.Update(1.0f / 60.0f, { 640.0f, 360.0f }, input); // the push frame skips OnTick
    CHECK_FALSE(ui.Empty());
    ui.Update(1.0f / 60.0f, { 640.0f, 360.0f }, input); // removes itself; must not crash
    CHECK(ui.Empty());
}

TEST_CASE("Close removes a specific screen wherever it sits", "[ui]")
{
    UIManager ui;
    auto      bottom    = MakeScreen();
    UIScreen* bottomPtr = bottom.get();
    ui.Push(std::move(bottom));
    auto      top    = MakeScreen(true);
    UIScreen* topPtr = top.get();
    ui.Push(std::move(top));

    ui.Close(bottomPtr); // no transition: removed at once
    CHECK(ui.Top() == topPtr);
    ui.Close(topPtr); // StaysOnTop screens close too (Pop skips them)
    CHECK(ui.Empty());
    ui.Close(topPtr); // no longer on the stack: no-op
}

TEST_CASE("Focus is dropped when the focused widget is destroyed", "[ui]")
{
    // Regression: rebinding with a controller rebuilt the controls list while
    // a chip button had focus; the screen kept the dangling pointer and the
    // next UI update crashed on it.
    UIScreen screen;
    auto*    list      = screen.Add<Panel>();
    auto*    button    = list->AddChild<Button>();
    button->Focusable = true;
    screen.FocusNext();
    REQUIRE(screen.GetFocusedWidget() == button);

    list->ClearChildren(); // the rebuild
    CHECK(screen.GetFocusedWidget() == nullptr);
    screen.FocusNext(); // and nothing touches the old widget
    screen.ActivateFocused();
}

TEST_CASE("Focus can be put back by its place in the focus order", "[ui]")
{
    UIScreen screen;
    auto*    list = screen.Add<Panel>();
    for (int i = 0; i < 3; ++i) list->AddChild<Button>()->Focusable = true;
    screen.FocusNext();
    screen.FocusNext();
    const i32 place = screen.GetFocusIndex();
    CHECK(place == 1);

    list->ClearChildren();
    std::vector<Button*> rebuilt;
    for (int i = 0; i < 3; ++i) {
        rebuilt.push_back(list->AddChild<Button>());
        rebuilt.back()->Focusable = true;
    }
    screen.FocusAt(place);
    CHECK(screen.GetFocusedWidget() == rebuilt[1]);
    screen.FocusAt(10); // past the end: the last one
    CHECK(screen.GetFocusedWidget() == rebuilt[2]);
}
