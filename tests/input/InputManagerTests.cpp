// InputManager without a window: events are injected the way Application
// forwards them from SDL, and ProcessEdges() turns them into per-tick edges.
// (IsPressed polls SDL's live keyboard state in Update(), so it isn't covered
// here — only the event-driven edge path.)

#include <arcbit/input/InputManager.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
constexpr ActionID Jump  = MakeAction("Jump");
constexpr ActionID Shoot = MakeAction("Shoot");

void Tap(InputManager& input, const SDL_Scancode sc, const bool down)
{
    input.InjectKeyEvent(static_cast<i32>(sc), down);
    input.ProcessEdges();
}
} // namespace

TEST_CASE("MakeAction is a stable compile-time hash", "[input]")
{
    STATIC_CHECK(MakeAction("Jump") == MakeAction("Jump"));
    STATIC_CHECK(MakeAction("Jump") != MakeAction("jump"));
}

TEST_CASE("Registration stores names and categories", "[input]")
{
    InputManager input;
    input.RegisterAction(Jump, "Jump", "Jump!", "Movement");
    CHECK(input.GetActionName(Jump) == "Jump");
    CHECK(input.GetActionDisplayName(Jump) == "Jump!");
    CHECK(input.GetActionCategory(Jump) == "Movement");
    CHECK(input.GetAllActions().size() == 1);
    CHECK(input.GetActionName(Shoot).empty());
}

TEST_CASE("A key press is JustPressed for exactly one tick", "[input]")
{
    InputManager input;
    input.RegisterAction(Jump, "Jump");
    input.BindKey(Jump, Key::Space);

    Tap(input, SDL_SCANCODE_SPACE, true);
    CHECK(input.JustPressed(Jump));
    CHECK(input.GetAnyJustPressedKey() == Key::Space);

    input.ProcessEdges(); // no new events
    CHECK_FALSE(input.JustPressed(Jump));

    Tap(input, SDL_SCANCODE_SPACE, false);
    CHECK(input.JustReleased(Jump));
    CHECK(input.GetAnyJustReleasedKey() == Key::Space);
}

TEST_CASE("Events between ticks are not lost", "[input]")
{
    // Several display frames can pass without a fixed tick; the press must
    // survive until the next ProcessEdges.
    InputManager input;
    input.BindKey(Jump, Key::Space);
    input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
    input.InjectKeyEvent(SDL_SCANCODE_SPACE, false); // released before the tick ran
    input.ProcessEdges();
    CHECK(input.JustPressed(Jump));
}

TEST_CASE("Any binding of an action triggers it", "[input]")
{
    InputManager input;
    input.BindKey(Shoot, Key::F);
    input.BindMouseButton(Shoot, MouseButton::Left);
    input.BindGamepadButton(Shoot, GamepadButton::RightShoulder);

    Tap(input, SDL_SCANCODE_F, true);
    CHECK(input.JustPressed(Shoot));

    input.InjectMouseButton(SDL_BUTTON_LEFT, true);
    input.ProcessEdges();
    CHECK(input.JustPressed(Shoot));
    CHECK(input.IsMouseButtonJustDown(MouseButton::Left));

    input.InjectGamepadButton(0, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    input.ProcessEdges();
    CHECK(input.JustPressed(Shoot));
    CHECK(input.GetAnyJustPressedGamepadButton() == GamepadButton::RightShoulder);

    Tap(input, SDL_SCANCODE_G, true); // unbound key
    CHECK_FALSE(input.JustPressed(Shoot));
}

TEST_CASE("Rebinding helpers remove the right bindings", "[input]")
{
    InputManager input;
    input.BindKey(Jump, Key::Space);
    input.BindKey(Shoot, Key::Space);
    input.BindGamepadButton(Jump, GamepadButton::South);
    input.BindMouseButton(Jump, MouseButton::Right);

    SECTION("UnbindKey removes the key from every action")
    {
        input.UnbindKey(Key::Space);
        CHECK(input.GetBindings(Shoot).empty());
        CHECK(input.GetBindings(Jump).size() == 2);
        Tap(input, SDL_SCANCODE_SPACE, true);
        CHECK_FALSE(input.JustPressed(Jump));
    }
    SECTION("ClearKBMBindings keeps gamepad bindings")
    {
        input.ClearKBMBindings(Jump);
        REQUIRE(input.GetBindings(Jump).size() == 1);
        CHECK(input.GetBindings(Jump)[0].BindingType == Binding::Type::GamepadButton);
    }
    SECTION("ClearGamepadBindings keeps keyboard and mouse")
    {
        input.ClearGamepadBindings(Jump);
        CHECK(input.GetBindings(Jump).size() == 2);
    }
    SECTION("ClearBindings empties one action only")
    {
        input.ClearBindings(Jump);
        CHECK(input.GetBindings(Jump).empty());
        CHECK(input.GetBindings(Shoot).size() == 1);
    }
}

TEST_CASE("Scroll delta accumulates between ticks and publishes once", "[input]")
{
    InputManager input;
    input.InjectMouseScroll(1.0f);
    input.InjectMouseScroll(2.0f);
    input.ProcessEdges();
    CHECK(input.GetScrollDelta() == 3.0f);
    input.ProcessEdges();
    CHECK(input.GetScrollDelta() == 0.0f);
}

TEST_CASE("Axis bindings apply the deadzone, then read their half", "[input]")
{
    const Binding full = Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Full, 0.2f);
    const Binding neg  = Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Negative, 0.2f);
    const Binding pos  = Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Positive, 0.2f);

    CHECK(full.ShapeAxis(-0.1f) == 0.0f); // inside the deadzone
    CHECK(full.ShapeAxis(-0.5f) == -0.5f);
    CHECK(full.ShapeAxis(0.5f) == 0.5f);

    // Regression: one full axis bound to Move_Left and Move_Right pressed
    // both whenever the stick moved. Each half only sees its own direction.
    CHECK(neg.ShapeAxis(-0.5f) == 0.5f);
    CHECK(neg.ShapeAxis(0.5f) == 0.0f);
    CHECK(pos.ShapeAxis(0.5f) == 0.5f);
    CHECK(pos.ShapeAxis(-0.5f) == 0.0f);
    CHECK(pos.ShapeAxis(0.1f) == 0.0f);
}

TEST_CASE("Axis halves are distinct bindings with their own labels", "[input]")
{
    const Binding left  = Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Negative);
    const Binding right = Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Positive);
    CHECK_FALSE(left == right);
    CHECK(left == Binding::FromGamepadAxis(GamepadAxis::LeftX, AxisHalf::Negative, 0.5f)); // deadzone ignored

    CHECK(InputManager::BindingToString(left) == "LS-Left");
    CHECK(InputManager::BindingToString(Binding::FromGamepadAxis(GamepadAxis::RightY, AxisHalf::Negative)) ==
          "RS-Up");
    CHECK(InputManager::BindingToString(Binding::FromGamepadAxis(GamepadAxis::LeftY)) == "LS-Y");
}

TEST_CASE("The last-used device follows key and gamepad presses", "[input]")
{
    InputManager input;
    CHECK(input.GetLastDevice() == InputDevice::KeyboardMouse); // the starting assumption
    input.InjectKeyEvent(SDL_SCANCODE_SPACE, true);
    CHECK_FALSE(input.ConsumeDeviceChanged()); // already keyboard / mouse

    input.InjectGamepadButton(1, SDL_GAMEPAD_BUTTON_SOUTH, true);
    CHECK(input.GetLastDevice() == InputDevice::Gamepad);
    CHECK(input.ConsumeDeviceChanged());
    CHECK_FALSE(input.ConsumeDeviceChanged()); // reported once

    input.InjectGamepadButton(1, SDL_GAMEPAD_BUTTON_SOUTH, false); // releases don't count
    input.InjectMouseScroll(1.0f);
    CHECK(input.GetLastDevice() == InputDevice::KeyboardMouse);
    CHECK(input.ConsumeDeviceChanged());
}

TEST_CASE("Releasing a key doesn't switch the device", "[input]")
{
    InputManager input;
    input.InjectGamepadButton(1, SDL_GAMEPAD_BUTTON_SOUTH, true);
    (void)input.ConsumeDeviceChanged();
    input.InjectKeyEvent(SDL_SCANCODE_SPACE, false); // a key held from before, let go
    CHECK(input.GetLastDevice() == InputDevice::Gamepad);
    CHECK(ToString(InputDevice::Gamepad) == "gamepad");
}
