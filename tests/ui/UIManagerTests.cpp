// UIManager's screen stack, without a renderer: layers, Push / Pop / Close,
// menus swapping each other out, and which screen gets the input.

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace Arcbit;

namespace
{
std::unique_ptr<UIScreen> MakeScreen(const UILayer layer = UILayer::Menu)
{
    auto screen             = std::make_unique<UIScreen>();
    screen->TransitionSpeed = 0.0f; // closes at once
    screen->Layer           = layer;
    return screen;
}

// Push and return the raw pointer (owned by the manager).
UIScreen* PushScreen(UIManager& ui, std::unique_ptr<UIScreen> screen)
{
    UIScreen* raw = screen.get();
    ui.Push(std::move(screen));
    return raw;
}

// Counts its input ticks (only the screen input goes to gets them) and its
// display updates (every screen gets them).
struct CountingScreen : UIScreen
{
    int Ticks   = 0;
    int Updates = 0;
    void OnTick(f32, const InputManager&) override { ++Ticks; }
    void OnUpdate(f32) override { ++Updates; }
};

void Tick(UIManager& ui, InputManager& input) { ui.Update(1.0f / 60.0f, { 640.0f, 360.0f }, input); }
} // namespace

TEST_CASE("Screens are pushed on top of their own layer", "[ui]")
{
    // Regression (splash): the first scene loads under the splash and pushes
    // its HUD; the HUD landed on top and the splash stopped ticking.
    UIManager ui;
    UIScreen* splash  = PushScreen(ui, MakeScreen(UILayer::System));
    UIScreen* overlay = PushScreen(ui, MakeScreen(UILayer::Overlay));
    UIScreen* hud     = PushScreen(ui, MakeScreen(UILayer::Hud));
    UIScreen* menu    = PushScreen(ui, MakeScreen(UILayer::Menu));
    CHECK(ui.Top() == splash);

    ui.Remove(splash);
    CHECK(ui.Top() == overlay); // the menu went under the overlay
    ui.Remove(overlay);
    CHECK(ui.Top() == menu); // and above the HUD
    ui.Remove(menu);
    CHECK(ui.Top() == hud);
}

TEST_CASE("Pop closes the topmost menu and nothing else", "[ui]")
{
    UIManager ui;
    UIScreen* hud     = PushScreen(ui, MakeScreen(UILayer::Hud));
    UIScreen* menu    = PushScreen(ui, MakeScreen(UILayer::Menu));
    UIScreen* overlay = PushScreen(ui, MakeScreen(UILayer::Overlay));
    ui.Pop();
    CHECK_FALSE(ui.IsOpen(menu));
    CHECK(ui.IsOpen(overlay));
    ui.Pop(); // no menus left: the HUD and overlay stay
    CHECK(ui.IsOpen(hud));
    CHECK(ui.IsOpen(overlay));
}

TEST_CASE("OpenMenu swaps out the open menus, sub-screens included", "[ui]")
{
    UIManager ui;
    UIScreen* hud      = PushScreen(ui, MakeScreen(UILayer::Hud));
    UIScreen* pause    = PushScreen(ui, MakeScreen());
    UIScreen* settings = PushScreen(ui, MakeScreen()); // opened from the pause menu
    CHECK(ui.HasOpenMenu());

    auto      picker    = MakeScreen();
    UIScreen* pickerPtr = picker.get();
    ui.OpenMenu(std::move(picker));
    CHECK_FALSE(ui.IsOpen(pause));
    CHECK_FALSE(ui.IsOpen(settings));
    CHECK(ui.IsOpen(pickerPtr));
    CHECK(ui.IsOpen(hud)); // not a menu
}

TEST_CASE("Input goes to the topmost screen that takes it", "[ui]")
{
    // Regression: the F3 event log sat on top and took the keys, so a menu
    // opened under it couldn't be used.
    UIManager    ui;
    InputManager input;
    auto         menu    = std::make_unique<CountingScreen>();
    auto         log     = std::make_unique<CountingScreen>();
    auto*        menuPtr = menu.get();
    auto*        logPtr  = log.get();
    log->Layer           = UILayer::Overlay;
    log->TakesInput      = false;
    ui.Push(std::move(menu));
    ui.Push(std::move(log));

    Tick(ui, input); // the push frame skips the tick
    Tick(ui, input);
    CHECK(menuPtr->Ticks == 1);
    CHECK(logPtr->Ticks == 0);

    // Regression (PR #5 review): the log took no input, so it got no updates
    // either and froze on its first frame - so did the loading screen.
    CHECK(logPtr->Updates == 2);
    CHECK(menuPtr->Updates == 2);
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
    UIScreen* bottom = PushScreen(ui, MakeScreen());
    UIScreen* top    = PushScreen(ui, MakeScreen(UILayer::System));

    ui.Close(bottom); // no transition: removed at once
    CHECK(ui.Top() == top);
    ui.Close(top); // any layer (Pop only touches menus)
    CHECK(ui.Empty());
    ui.Close(top); // no longer on the stack: no-op
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

TEST_CASE("A scroll panel scrolls its focused child into view", "[ui]")
{
    // With D-pad / arrow navigation, focus walked off the visible part of a
    // long list (the controls screen) and the list never followed.
    UIScreen screen;
    auto*    list       = screen.Add<ScrollPanel>();
    list->Size          = { 300.0f, 200.0f };
    list->ContentHeight = 1000.0f;
    std::vector<Button*> rows;
    for (int i = 0; i < 20; ++i) {
        auto* row      = list->AddChild<Button>();
        row->Size      = { 280.0f, 40.0f };
        row->Offset    = { 0.0f, 50.0f * static_cast<f32>(i) };
        row->Focusable = true;
        rows.push_back(row);
    }
    const UIRect window{ 0.0f, 0.0f, 640.0f, 360.0f };
    const auto   update = [&] { screen.Update(1.0f / 60.0f, window, { -1.0f, -1.0f }, false, false, false); };

    for (int i = 0; i <= 10; ++i) screen.FocusNext(); // row 10: y 500..540, well below the 200 px view
    update();
    CHECK(list->ScrollOffset >= 540.0f - 200.0f); // its bottom is in view
    CHECK(list->ScrollOffset <= 500.0f);          // and so is its top

    screen.FocusAt(0);
    update();
    CHECK(list->ScrollOffset == 0.0f); // back to the top
}

TEST_CASE("Held navigation steps once, waits, then repeats until released", "[ui]")
{
    constexpr f32    Dt = 1.0f / 60.0f;
    NavRepeat        nav; // 0.4 s delay, 0.08 s interval
    std::vector<i32> steps;
    steps.push_back(nav.Step(true, false, true, false, Dt)); // the press
    for (int i = 0; i < 59; ++i) steps.push_back(nav.Step(false, false, true, false, Dt)); // held 1 s in all

    CHECK(steps[0] == 1);
    // Nothing until the delay has passed: 0.4 s is 24 ticks after the press.
    for (int i = 1; i < 24; ++i) CHECK(steps[i] == 0);
    i32 repeats = 0;
    for (const i32 s : steps) repeats += s;
    // The press, then about one step per 0.08 s for the remaining 0.6 s.
    CHECK(repeats >= 7);
    CHECK(repeats <= 9);

    CHECK(nav.Step(false, false, false, false, Dt) == 0); // released: stops at once
    CHECK(nav.Step(false, false, true, false, Dt) == 0);  // held again without a press: no repeat
}

TEST_CASE("The newest direction pressed wins", "[ui]")
{
    constexpr f32 Dt = 1.0f / 60.0f;
    NavRepeat     nav;
    CHECK(nav.Step(true, false, true, false, Dt) == 1);
    CHECK(nav.Step(false, true, true, true, Dt) == -1); // previous pressed while next is still held
    for (int i = 0; i < 30; ++i) (void)nav.Step(false, false, true, true, Dt);
    CHECK(nav.Step(false, false, true, true, 0.1f) == -1); // repeating the previous direction
}
