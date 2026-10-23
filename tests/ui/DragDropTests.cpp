// Drag and drop, tooltips and GridPanel (docs/gui.md): driven through a
// headless UIScreen's Interact the way UIManager feeds it, and through a
// UIManager with the keys bound by hand.

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
constexpr UIRect Screen = { 0.0f, 0.0f, 800.0f, 600.0f };
constexpr f32    Dt     = 1.0f / 60.0f;

UIPointer At(const Vec2 pos, const bool down = false) { return { .Pos = pos, .Down = down }; }
UIPointer Press(const Vec2 pos) { return { .Pos = pos, .Down = true, .JustDown = true }; }
UIPointer Release(const Vec2 pos) { return { .Pos = pos, .JustUp = true }; }

Panel* Box(UIScreen& screen, const Vec2 at)
{
    Panel* p  = screen.Add<Panel>();
    p->Offset = at;
    p->Size   = { 50.0f, 50.0f };
    return p;
}

// A source that hands out item 7 (and says how its drag ended), and a
// target that takes items only.
struct Fixture
{
    UIScreen                  screen;
    UISkin                    skin;
    Panel*                    source = Box(screen, { 0.0f, 0.0f });
    Panel*                    target = Box(screen, { 100.0f, 0.0f });
    std::optional<DragStart>  started;
    std::optional<bool>       ended;
    std::vector<i32>          dropped;

    Fixture()
    {
        source->OnDragStart = [this](const DragStart& how) -> std::optional<DragPayload> {
            started = how;
            return DragPayload{ .Kind = "item", .Data = 7, .Rotatable = true, .Size = { 20.0f, 40.0f } };
        };
        source->OnDragEnd = [this](const DragPayload&, const bool wasDropped) { ended = wasDropped; };
        target->CanDrop   = [](const DragPayload& p) { return p.Kind == "item"; };
        target->OnDrop    = [this](const DragPayload& p) { dropped.push_back(std::any_cast<i32>(p.Data)); };
    }
    UIPointer Feed(const UIPointer& p) { return screen.Interact(Dt, Screen, p, skin); }
};
} // namespace

TEST_CASE("A press on a source drags once it moves; letting go on a target drops", "[ui][drag]")
{
    Fixture f;
    (void)f.Feed(Press({ 10.0f, 10.0f }));
    (void)f.Feed(At({ 13.0f, 12.0f }, true));
    CHECK_FALSE(f.screen.IsDragging()); // a few pixels: still a click

    const UIPointer seen = f.Feed(At({ 120.0f, 20.0f }, true));
    REQUIRE(f.screen.IsDragging());
    CHECK_FALSE(seen.Down); // nothing under a drag sees the button
    CHECK(f.source->IsBeingDragged());
    CHECK(f.target->IsDragOver());
    CHECK(f.screen.GetDropTarget() == f.target);
    CHECK_FALSE(f.started->Alternate);

    (void)f.Feed(Release({ 120.0f, 20.0f }));
    CHECK_FALSE(f.screen.IsDragging());
    CHECK(f.dropped == std::vector<i32>{ 7 });
    CHECK(f.ended == true);
    CHECK_FALSE(f.source->IsBeingDragged());
    CHECK_FALSE(f.target->IsDragOver());
}

TEST_CASE("Dropped nowhere, refused, or cancelled: the source hears nothing was dropped", "[ui][drag]")
{
    Fixture f;
    const auto drag = [&](const Vec2 to) {
        f.ended.reset();
        (void)f.Feed(Press({ 10.0f, 10.0f }));
        (void)f.Feed(At(to, true));
    };
    drag({ 300.0f, 300.0f });
    (void)f.Feed(Release({ 300.0f, 300.0f }));
    CHECK(f.ended == false);

    f.target->CanDrop = [](const DragPayload& p) { return p.Kind == "quick_slot"; };
    drag({ 120.0f, 20.0f });
    CHECK(f.target->IsDragOver()); // over it - drawn as refusing
    (void)f.Feed(Release({ 120.0f, 20.0f }));
    CHECK(f.ended == false);
    CHECK(f.dropped.empty());

    drag({ 120.0f, 20.0f });
    f.screen.CancelDrag();
    CHECK_FALSE(f.screen.IsDragging());
    CHECK(f.ended == false);
}

TEST_CASE("The right button drags the alternate way; R turns what's held", "[ui][drag]")
{
    Fixture f;
    (void)f.Feed({ .Pos = { 10.0f, 10.0f }, .AltDown = true, .AltJustDown = true });
    (void)f.Feed({ .Pos = { 120.0f, 20.0f }, .AltDown = true });
    REQUIRE(f.screen.IsDragging());
    CHECK(f.started->Alternate);

    f.screen.RotateDrag();
    CHECK(f.screen.GetDrag()->Rotated);
    CHECK(f.screen.GetDrag()->HeldSize() == Vec2{ 40.0f, 20.0f });

    (void)f.Feed({ .Pos = { 120.0f, 20.0f }, .AltJustUp = true });
    CHECK(f.dropped == std::vector<i32>{ 7 });
}

TEST_CASE("The grab key picks up from the focused widget and puts down where focus goes", "[ui][drag]")
{
    Fixture f;
    f.source->Focusable = f.target->Focusable = true;
    f.screen.FocusAt(0);
    f.screen.GrabFocused(false);
    REQUIRE(f.screen.IsDragging());
    CHECK(f.started->Keyboard);

    f.screen.FocusAt(1);
    f.screen.FocusMovedByKeys(); // what UIManager says when the arrows move focus
    CHECK(f.target->IsDragOver());
    (void)f.Feed(At({ 500.0f, 500.0f })); // the pointer has no say in a key drag
    CHECK(f.screen.IsDragging());
    f.screen.GrabFocused(false);
    CHECK(f.dropped == std::vector<i32>{ 7 });
    CHECK(f.ended == true);

    f.screen.FocusAt(0); // onto a widget that isn't a target: still held
    f.screen.GrabFocused(false);
    f.screen.GrabFocused(false);
    CHECK(f.screen.IsDragging());
}

TEST_CASE("A tooltip shows after the pointer rests, and for focus moved by keys", "[ui][tooltip]")
{
    Fixture f;
    f.source->Tooltip   = "A potion";
    f.target->GetTooltip = [] { return std::string("Drop here"); };
    (void)f.Feed(At({ 10.0f, 10.0f }));
    CHECK(f.screen.GetTooltipText().empty());
    for (int i = 0; i < 40; ++i) (void)f.Feed(At({ 10.0f, 10.0f })); // past the skin's half second
    CHECK(f.screen.GetTooltipText() == "A potion");

    (void)f.Feed(At({ 120.0f, 10.0f }));
    CHECK(f.screen.GetTooltipText().empty()); // a new widget: its own wait
    for (int i = 0; i < 40; ++i) (void)f.Feed(At({ 120.0f, 10.0f }));
    CHECK(f.screen.GetTooltipText() == "Drop here");
    (void)f.Feed(Press({ 120.0f, 10.0f }));
    CHECK(f.screen.GetTooltipText().empty()); // a press hides it

    f.source->Focusable = true;
    f.screen.FocusAt(0);
    f.screen.FocusMovedByKeys();
    for (int i = 0; i < 40; ++i) (void)f.Feed(At({ 700.0f, 500.0f }));
    CHECK(f.screen.GetTooltipText() == "A potion");
    (void)f.Feed(At({ 650.0f, 450.0f })); // the mouse moves: back to the pointer's
    CHECK(f.screen.GetTooltipText().empty());
}

TEST_CASE("TooltipKey reads the player's language when shown", "[ui][tooltip]")
{
    Loc::Clear();
    const std::vector<std::pair<std::string, std::string>> strings = { { "ui.tip.potion", "Restores 50 HP." } };
    Loc::SetStrings(Loc::English, "test", strings);
    Fixture f;
    f.source->TooltipKey = "ui.tip.potion";
    for (int i = 0; i < 40; ++i) (void)f.Feed(At({ 10.0f, 10.0f }));
    CHECK(f.screen.GetTooltipText() == "Restores 50 HP.");
    Loc::Clear();
}

TEST_CASE("GridPanel lays out cells from a template and fits itself", "[ui][grid]")
{
    UIScreen   screen;
    GridPanel* grid = screen.Add<GridPanel>();
    grid->Columns   = 3;
    grid->CellSize  = { 40.0f, 30.0f };
    grid->Gap       = { 4.0f, 2.0f };
    grid->Padding   = { 5.0f, 5.0f };
    grid->Fit       = true;
    screen.SetTemplate("cell", R"({ "type": "Panel", "focusable": true })");
    REQUIRE(screen.AddFromTemplate("cell", *grid, 7).size() == 7);
    grid->Arrange();

    const std::vector<UIWidget*> cells = grid->GetCells();
    CHECK(cells[4]->Offset == Vec2{ 5.0f + 44.0f, 5.0f + 32.0f }); // second row, second column
    CHECK(cells[4]->Size == Vec2{ 40.0f, 30.0f });
    CHECK(grid->Size == Vec2{ 10.0f + 3 * 40.0f + 2 * 4.0f, 10.0f + 3 * 30.0f + 2 * 2.0f }); // 3 rows

    CHECK(grid->Neighbor(cells[4], 1, 0) == cells[5]);
    CHECK(grid->Neighbor(cells[4], 0, -1) == cells[1]);
    CHECK(grid->Neighbor(cells[5], 1, 0) == nullptr);  // the right edge
    CHECK(grid->Neighbor(cells[4], 0, 1) == nullptr);  // row three has only cell 6
    CHECK(grid->Neighbor(cells[3], 0, 1) == cells[6]);
    CHECK(screen.FindGridOf(cells[2]) == grid);
}

TEST_CASE("Arrows move across a grid; Space grabs and drops; Escape cancels a drag, not the screen", "[ui][grid][drag]")
{
    InputManager input;
    UIManager    ui;
    for (const auto& [action, name, key] : {
             std::tuple{ UIManager::ActionNavRight, "UI_NavRight", Key::Right }, std::tuple{ UIManager::ActionNavDown, "UI_NavDown", Key::Down },
             std::tuple{ UIManager::ActionGrab, "UI_Grab", Key::Space }, std::tuple{ UIManager::ActionTextEscape, "UI_TextEscape", Key::Escape } }) {
        input.RegisterAction(action, name);
        input.BindKey(action, key);
    }
    struct Screen final : UIScreen
    {
        int  Backs = 0;
        void OnBackPressed() override { ++Backs; }
    };
    auto       owned = std::make_unique<Screen>();
    Screen*    screen = owned.get();
    GridPanel* grid   = screen->Add<GridPanel>();
    grid->Columns     = 2;
    std::vector<int> drops;
    for (int i = 0; i < 4; ++i) {
        Panel* cell       = grid->AddChild<Panel>();
        cell->Focusable   = true;
        cell->OnDragStart = [i](const DragStart&) { return std::optional(DragPayload{ .Kind = "item", .Data = i }); };
        cell->OnDrop      = [&drops, i](const DragPayload& p) { drops.push_back(std::any_cast<int>(p.Data) * 10 + i); };
    }
    ui.Push(std::move(owned));
    const auto tick = [&] { ui.Update(Dt, { 800.0f, 600.0f }, input); };
    const auto press = [&](const SDL_Scancode key) {
        input.InjectKeyEvent(key, true);
        input.ProcessEdges();
        tick();
        input.InjectKeyEvent(key, false);
        input.ProcessEdges();
        tick();
    };
    tick();
    screen->FocusAt(0);

    press(SDL_SCANCODE_SPACE); // pick up cell 0
    press(SDL_SCANCODE_RIGHT);
    press(SDL_SCANCODE_DOWN);  // cell 3, a row down
    CHECK(screen->GetFocusIndex() == 3);
    press(SDL_SCANCODE_SPACE);
    CHECK(drops == std::vector<int>{ 3 }); // cell 0's payload onto cell 3

    press(SDL_SCANCODE_SPACE);
    press(SDL_SCANCODE_ESCAPE);
    CHECK_FALSE(screen->IsDragging());
    CHECK(screen->Backs == 0); // Escape went to the drag
    press(SDL_SCANCODE_ESCAPE);
    CHECK(screen->Backs == 1);
}
