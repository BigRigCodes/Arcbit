// UIManager's screen stack, without a renderer: Push / Pop ordering and the
// StaysOnTop rule.

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UIScreen.h>

#include <catch2/catch_test_macros.hpp>

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
