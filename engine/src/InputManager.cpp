#include <arcbit/input/InputManager.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------

const std::vector<Binding> InputManager::s_EmptyBindings;

// ---------------------------------------------------------------------------
// SDL mapping helpers
//
// These convert our engine enums to SDL constants. Keeping them here (not in
// the header) means SDL3/SDL.h stays out of the public API entirely.
// ---------------------------------------------------------------------------

namespace {

// Map Key → SDL_Scancode (physical keyboard position, layout-independent).
SDL_Scancode ToScancode(const Key key)
{
    switch (key)
    {
        // Letters
        case Key::A: return SDL_SCANCODE_A; case Key::B: return SDL_SCANCODE_B;
        case Key::C: return SDL_SCANCODE_C; case Key::D: return SDL_SCANCODE_D;
        case Key::E: return SDL_SCANCODE_E; case Key::F: return SDL_SCANCODE_F;
        case Key::G: return SDL_SCANCODE_G; case Key::H: return SDL_SCANCODE_H;
        case Key::I: return SDL_SCANCODE_I; case Key::J: return SDL_SCANCODE_J;
        case Key::K: return SDL_SCANCODE_K; case Key::L: return SDL_SCANCODE_L;
        case Key::M: return SDL_SCANCODE_M; case Key::N: return SDL_SCANCODE_N;
        case Key::O: return SDL_SCANCODE_O; case Key::P: return SDL_SCANCODE_P;
        case Key::Q: return SDL_SCANCODE_Q; case Key::R: return SDL_SCANCODE_R;
        case Key::S: return SDL_SCANCODE_S; case Key::T: return SDL_SCANCODE_T;
        case Key::U: return SDL_SCANCODE_U; case Key::V: return SDL_SCANCODE_V;
        case Key::W: return SDL_SCANCODE_W; case Key::X: return SDL_SCANCODE_X;
        case Key::Y: return SDL_SCANCODE_Y; case Key::Z: return SDL_SCANCODE_Z;

        // Number row
        case Key::Num0: return SDL_SCANCODE_0; case Key::Num1: return SDL_SCANCODE_1;
        case Key::Num2: return SDL_SCANCODE_2; case Key::Num3: return SDL_SCANCODE_3;
        case Key::Num4: return SDL_SCANCODE_4; case Key::Num5: return SDL_SCANCODE_5;
        case Key::Num6: return SDL_SCANCODE_6; case Key::Num7: return SDL_SCANCODE_7;
        case Key::Num8: return SDL_SCANCODE_8; case Key::Num9: return SDL_SCANCODE_9;

        // Function keys
        case Key::F1:  return SDL_SCANCODE_F1;  case Key::F2:  return SDL_SCANCODE_F2;
        case Key::F3:  return SDL_SCANCODE_F3;  case Key::F4:  return SDL_SCANCODE_F4;
        case Key::F5:  return SDL_SCANCODE_F5;  case Key::F6:  return SDL_SCANCODE_F6;
        case Key::F7:  return SDL_SCANCODE_F7;  case Key::F8:  return SDL_SCANCODE_F8;
        case Key::F9:  return SDL_SCANCODE_F9;  case Key::F10: return SDL_SCANCODE_F10;
        case Key::F11: return SDL_SCANCODE_F11; case Key::F12: return SDL_SCANCODE_F12;

        // Navigation
        case Key::Left:     return SDL_SCANCODE_LEFT;
        case Key::Right:    return SDL_SCANCODE_RIGHT;
        case Key::Up:       return SDL_SCANCODE_UP;
        case Key::Down:     return SDL_SCANCODE_DOWN;
        case Key::Home:     return SDL_SCANCODE_HOME;
        case Key::End:      return SDL_SCANCODE_END;
        case Key::PageUp:   return SDL_SCANCODE_PAGEUP;
        case Key::PageDown: return SDL_SCANCODE_PAGEDOWN;
        case Key::Insert:   return SDL_SCANCODE_INSERT;
        case Key::Delete:   return SDL_SCANCODE_DELETE;

        // Modifiers
        case Key::LeftShift:  return SDL_SCANCODE_LSHIFT;
        case Key::RightShift: return SDL_SCANCODE_RSHIFT;
        case Key::LeftCtrl:   return SDL_SCANCODE_LCTRL;
        case Key::RightCtrl:  return SDL_SCANCODE_RCTRL;
        case Key::LeftAlt:    return SDL_SCANCODE_LALT;
        case Key::RightAlt:   return SDL_SCANCODE_RALT;
        case Key::LeftSuper:  return SDL_SCANCODE_LGUI;
        case Key::RightSuper: return SDL_SCANCODE_RGUI;

        // Special
        case Key::Space:       return SDL_SCANCODE_SPACE;
        case Key::Enter:       return SDL_SCANCODE_RETURN;
        case Key::Escape:      return SDL_SCANCODE_ESCAPE;
        case Key::Tab:         return SDL_SCANCODE_TAB;
        case Key::Backspace:   return SDL_SCANCODE_BACKSPACE;
        case Key::CapsLock:    return SDL_SCANCODE_CAPSLOCK;
        case Key::NumLock:     return SDL_SCANCODE_NUMLOCKCLEAR;
        case Key::ScrollLock:  return SDL_SCANCODE_SCROLLLOCK;
        case Key::PrintScreen: return SDL_SCANCODE_PRINTSCREEN;
        case Key::Pause:       return SDL_SCANCODE_PAUSE;

        // Punctuation
        case Key::Comma:        return SDL_SCANCODE_COMMA;
        case Key::Period:       return SDL_SCANCODE_PERIOD;
        case Key::Slash:        return SDL_SCANCODE_SLASH;
        case Key::Backslash:    return SDL_SCANCODE_BACKSLASH;
        case Key::Semicolon:    return SDL_SCANCODE_SEMICOLON;
        case Key::Apostrophe:   return SDL_SCANCODE_APOSTROPHE;
        case Key::Grave:        return SDL_SCANCODE_GRAVE;
        case Key::LeftBracket:  return SDL_SCANCODE_LEFTBRACKET;
        case Key::RightBracket: return SDL_SCANCODE_RIGHTBRACKET;
        case Key::Minus:        return SDL_SCANCODE_MINUS;
        case Key::Equals:       return SDL_SCANCODE_EQUALS;

        // Numpad
        case Key::Kp0:        return SDL_SCANCODE_KP_0;
        case Key::Kp1:        return SDL_SCANCODE_KP_1;
        case Key::Kp2:        return SDL_SCANCODE_KP_2;
        case Key::Kp3:        return SDL_SCANCODE_KP_3;
        case Key::Kp4:        return SDL_SCANCODE_KP_4;
        case Key::Kp5:        return SDL_SCANCODE_KP_5;
        case Key::Kp6:        return SDL_SCANCODE_KP_6;
        case Key::Kp7:        return SDL_SCANCODE_KP_7;
        case Key::Kp8:        return SDL_SCANCODE_KP_8;
        case Key::Kp9:        return SDL_SCANCODE_KP_9;
        case Key::KpPlus:     return SDL_SCANCODE_KP_PLUS;
        case Key::KpMinus:    return SDL_SCANCODE_KP_MINUS;
        case Key::KpMultiply: return SDL_SCANCODE_KP_MULTIPLY;
        case Key::KpDivide:   return SDL_SCANCODE_KP_DIVIDE;
        case Key::KpEnter:    return SDL_SCANCODE_KP_ENTER;
        case Key::KpPeriod:   return SDL_SCANCODE_KP_PERIOD;

        default: return SDL_SCANCODE_UNKNOWN;
    }
}

// Map MouseButton → the SDL button index used by SDL_BUTTON_MASK.
// SDL3 mouse button indices: Left=1, Middle=2, Right=3, X1=4, X2=5.
i32 ToSDLMouseButton(const MouseButton button)
{
    switch (button)
    {
        case MouseButton::Left:   return SDL_BUTTON_LEFT;
        case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
        case MouseButton::Right:  return SDL_BUTTON_RIGHT;
        case MouseButton::X1:     return SDL_BUTTON_X1;
        case MouseButton::X2:     return SDL_BUTTON_X2;
        default:                  return SDL_BUTTON_LEFT;
    }
}

SDL_GamepadButton ToSDLGamepadButton(const GamepadButton button)
{
    switch (button)
    {
        case GamepadButton::South:         return SDL_GAMEPAD_BUTTON_SOUTH;
        case GamepadButton::East:          return SDL_GAMEPAD_BUTTON_EAST;
        case GamepadButton::West:          return SDL_GAMEPAD_BUTTON_WEST;
        case GamepadButton::North:         return SDL_GAMEPAD_BUTTON_NORTH;
        case GamepadButton::LeftShoulder:  return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case GamepadButton::RightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case GamepadButton::LeftStick:     return SDL_GAMEPAD_BUTTON_LEFT_STICK;
        case GamepadButton::RightStick:    return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        case GamepadButton::Start:         return SDL_GAMEPAD_BUTTON_START;
        case GamepadButton::Back:          return SDL_GAMEPAD_BUTTON_BACK;
        case GamepadButton::Guide:         return SDL_GAMEPAD_BUTTON_GUIDE;
        case GamepadButton::DPadUp:        return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case GamepadButton::DPadDown:      return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case GamepadButton::DPadLeft:      return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case GamepadButton::DPadRight:     return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        default:                           return SDL_GAMEPAD_BUTTON_SOUTH;
    }
}

SDL_GamepadAxis ToSDLGamepadAxis(const GamepadAxis axis)
{
    switch (axis)
    {
        case GamepadAxis::LeftX:         return SDL_GAMEPAD_AXIS_LEFTX;
        case GamepadAxis::LeftY:         return SDL_GAMEPAD_AXIS_LEFTY;
        case GamepadAxis::RightX:        return SDL_GAMEPAD_AXIS_RIGHTX;
        case GamepadAxis::RightY:        return SDL_GAMEPAD_AXIS_RIGHTY;
        case GamepadAxis::LeftTrigger:   return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        case GamepadAxis::RightTrigger:  return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        default:                         return SDL_GAMEPAD_AXIS_LEFTX;
    }
}

// Normalize a raw SDL axis value (Sint16, range -32768..32767) to [-1, 1].
// Triggers (LeftTrigger / RightTrigger) report [0..32767], so they map to
// [0, 1] naturally after this normalization.
f32 NormalizeAxis(const i16 raw)
{
    return raw >= 0
        ? static_cast<f32>(raw)  / 32767.0f
        : static_cast<f32>(raw)  / 32768.0f;
}

// Short label for an axis binding, e.g. "LS-X" or, for one half, "LS-Left".
std::string AxisToString(const GamepadAxis axis, const AxisHalf half)
{
    const bool  isX  = axis == GamepadAxis::LeftX || axis == GamepadAxis::RightX;
    const char* side = half == AxisHalf::Positive ? (isX ? "Right" : "Down")
                     : half == AxisHalf::Negative ? (isX ? "Left" : "Up")
                     : (isX ? "X" : "Y");
    switch (axis) {
        case GamepadAxis::LeftX:
        case GamepadAxis::LeftY:  return std::string("LS-") + side;
        case GamepadAxis::RightX:
        case GamepadAxis::RightY: return std::string("RS-") + side;
        default: return "Axis?";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Display helpers (static, no SDL headers needed in the header)
// ---------------------------------------------------------------------------

std::string_view InputManager::KeyToString(const Key key)
{
    switch (key)
    {
        case Key::A: return "A"; case Key::B: return "B"; case Key::C: return "C";
        case Key::D: return "D"; case Key::E: return "E"; case Key::F: return "F";
        case Key::G: return "G"; case Key::H: return "H"; case Key::I: return "I";
        case Key::J: return "J"; case Key::K: return "K"; case Key::L: return "L";
        case Key::M: return "M"; case Key::N: return "N"; case Key::O: return "O";
        case Key::P: return "P"; case Key::Q: return "Q"; case Key::R: return "R";
        case Key::S: return "S"; case Key::T: return "T"; case Key::U: return "U";
        case Key::V: return "V"; case Key::W: return "W"; case Key::X: return "X";
        case Key::Y: return "Y"; case Key::Z: return "Z";
        case Key::Num0: return "0"; case Key::Num1: return "1"; case Key::Num2: return "2";
        case Key::Num3: return "3"; case Key::Num4: return "4"; case Key::Num5: return "5";
        case Key::Num6: return "6"; case Key::Num7: return "7"; case Key::Num8: return "8";
        case Key::Num9: return "9";
        case Key::F1:  return "F1";  case Key::F2:  return "F2";  case Key::F3:  return "F3";
        case Key::F4:  return "F4";  case Key::F5:  return "F5";  case Key::F6:  return "F6";
        case Key::F7:  return "F7";  case Key::F8:  return "F8";  case Key::F9:  return "F9";
        case Key::F10: return "F10"; case Key::F11: return "F11"; case Key::F12: return "F12";
        case Key::Left:  return "Left";  case Key::Right: return "Right";
        case Key::Up:    return "Up";    case Key::Down:  return "Down";
        case Key::Home:  return "Home";  case Key::End:   return "End";
        case Key::PageUp:   return "PgUp"; case Key::PageDown: return "PgDn";
        case Key::Insert:   return "Ins";  case Key::Delete:   return "Del";
        case Key::LeftShift:  return "LShift";  case Key::RightShift: return "RShift";
        case Key::LeftCtrl:   return "LCtrl";   case Key::RightCtrl:  return "RCtrl";
        case Key::LeftAlt:    return "LAlt";     case Key::RightAlt:   return "RAlt";
        case Key::LeftSuper:  return "LSuper";   case Key::RightSuper: return "RSuper";
        case Key::Space:     return "Space";  case Key::Enter:     return "Enter";
        case Key::Escape:    return "Escape"; case Key::Tab:       return "Tab";
        case Key::Backspace: return "Bksp";   case Key::CapsLock:  return "Caps";
        case Key::Comma:     return ",";      case Key::Period:    return ".";
        case Key::Slash:     return "/";      case Key::Backslash: return "\\";
        case Key::Semicolon: return ";";      case Key::Apostrophe:return "'";
        case Key::Grave:     return "`";      case Key::Minus:     return "-";
        case Key::Equals:    return "=";
        case Key::Kp0: return "Kp0"; case Key::Kp1: return "Kp1"; case Key::Kp2: return "Kp2";
        case Key::Kp3: return "Kp3"; case Key::Kp4: return "Kp4"; case Key::Kp5: return "Kp5";
        case Key::Kp6: return "Kp6"; case Key::Kp7: return "Kp7"; case Key::Kp8: return "Kp8";
        case Key::Kp9: return "Kp9"; case Key::KpPlus: return "Kp+"; case Key::KpMinus: return "Kp-";
        case Key::KpMultiply: return "Kp*"; case Key::KpDivide: return "Kp/";
        case Key::KpEnter: return "KpEnter"; case Key::KpPeriod: return "Kp.";
        default: return "---";
    }
}

std::string InputManager::BindingToString(const Binding& b)
{
    switch (b.BindingType)
    {
        case Binding::Type::Key:
            return std::string(KeyToString(b.BoundKey));
        case Binding::Type::MouseButton:
            switch (b.BoundMouse) {
                case MouseButton::Left:   return "LMB";
                case MouseButton::Right:  return "RMB";
                case MouseButton::Middle: return "MMB";
                default: return "Mouse?";
            }
        case Binding::Type::GamepadButton:
            switch (b.BoundButton) {
                case GamepadButton::South:         return "Pad A";
                case GamepadButton::East:          return "Pad B";
                case GamepadButton::West:          return "Pad X";
                case GamepadButton::North:         return "Pad Y";
                case GamepadButton::LeftShoulder:  return "LB";
                case GamepadButton::RightShoulder: return "RB";
                case GamepadButton::DPadUp:        return "D-Up";
                case GamepadButton::DPadDown:      return "D-Dn";
                case GamepadButton::DPadLeft:      return "D-Lt";
                case GamepadButton::DPadRight:     return "D-Rt";
                case GamepadButton::Start:         return "Start";
                default: return "Pad?";
            }
        case Binding::Type::GamepadAxis:
            return AxisToString(b.BoundAxis, b.Half);
        default: return "---";
    }
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

InputManager::InputManager() = default;

InputManager::~InputManager()
{
    // Close all open gamepad handles so SDL can release its resources.
    for (auto& handle : _gamepads | std::views::values)
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(handle));
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void InputManager::RegisterAction(const ActionID action, const std::string_view name,
                                   const std::string_view displayName,
                                   const std::string_view category)
{
    auto& entry       = GetOrCreate(action);
    entry.Name        = std::string(name);
    entry.DisplayName = std::string(displayName.empty() ? name : displayName);
    entry.Category    = std::string(category);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void InputManager::BindKey(const ActionID action, const Key key)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromKey(key));
}

void InputManager::BindMouseButton(const ActionID action, const MouseButton button)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromMouseButton(button));
}

void InputManager::BindGamepadButton(const ActionID action, const GamepadButton button)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromGamepadButton(button));
}

void InputManager::BindGamepadAxis(const ActionID action, const GamepadAxis axis, const AxisHalf half,
                                   const f32 deadzone)
{
    GetOrCreate(action).Bindings.push_back(Binding::FromGamepadAxis(axis, half, deadzone));
}

void InputManager::ClearBindings(const ActionID action)
{
    if (const auto it = _actions.find(action); it != _actions.end())
        it->second.Bindings.clear();
}

// ---------------------------------------------------------------------------
// Input event injection — called by Application during Window::PollEvents
// ---------------------------------------------------------------------------

void InputManager::InjectKeyEvent(const i32 scancode, const bool down)
{
    if (down) _pendingKeyDown.insert(scancode);
    else       _pendingKeyUp.insert(scancode);
    if (down) NoteDevice(InputDevice::KeyboardMouse);
}

void InputManager::InjectMouseButton(const i32 sdlButton, const bool down)
{
    if (down) _pendingMouseDown.insert(sdlButton);
    else       _pendingMouseUp.insert(sdlButton);
    if (down) NoteDevice(InputDevice::KeyboardMouse);
}

void InputManager::InjectGamepadButton(const u32 joystickId, const i32 sdlButton, const bool down)
{
    if (down) _pendingGamepadDown.push_back({ joystickId, sdlButton });
    else       _pendingGamepadUp.push_back({ joystickId, sdlButton });
    if (down) NoteDevice(InputDevice::Gamepad);
}

// Real mouse movement (not a pixel of jitter) means keyboard / mouse; a stick
// or trigger *pushed* past half way means gamepad - so a player who only moves
// the stick or the mouse still switches the button hints. Only the crossing
// counts: a stick or trigger merely held there doesn't keep reclaiming the
// device from a newer key press or mouse movement.
void InputManager::DetectDeviceFromMotion()
{
    constexpr i32 MouseMovePixels = 4;
    constexpr i16 StickPush       = 16384; // half of SDL's axis range

    const bool seen = std::exchange(_mouseSeen, true);
    if (seen && std::abs(_mouseX - _prevMouseX) + std::abs(_mouseY - _prevMouseY) >= MouseMovePixels)
        NoteDevice(InputDevice::KeyboardMouse);

    u32 pushed = 0; // a bit per axis past half way, on any gamepad
    for (void* handle : _gamepads | std::views::values) {
        auto* gamepad = static_cast<SDL_Gamepad*>(handle);
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
            if (std::abs(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(axis))) >= StickPush)
                pushed |= 1u << axis;
    }
    if (pushed & ~_axesPushed) NoteDevice(InputDevice::Gamepad);
    _axesPushed = pushed;
}

// ---------------------------------------------------------------------------
// Update — called once per display frame after Window::PollEvents
// ---------------------------------------------------------------------------

void InputManager::Update()
{
    // --- Keyboard -----------------------------------------------------------
    i32 numKeys = 0;
    _keyState   = SDL_GetKeyboardState(&numKeys);

    // --- Mouse --------------------------------------------------------------
    _prevMouseX = _mouseX;
    _prevMouseY = _mouseY;

    f32 mx = 0.0f, my = 0.0f;
    _mouseButtonMask = SDL_GetMouseState(&mx, &my);
    _mouseX = static_cast<i32>(mx);
    _mouseY = static_cast<i32>(my);

    // --- Gamepad connect / disconnect ---------------------------------------
    i32 gamepadCount = 0;

    if (SDL_JoystickID* connectedIDs = SDL_GetGamepads(&gamepadCount))
    {
        std::vector<u32> currentIDs(connectedIDs, connectedIDs + gamepadCount);
        SDL_free(connectedIDs);

        for (u32 id : currentIDs)
        {
            if (!_gamepads.contains(id))
            {
                if (SDL_Gamepad* gp = SDL_OpenGamepad(id))
                {
                    _gamepads[id] = gp;
                    LOG_INFO(Platform, "Gamepad connected: {} (id={})",
                        SDL_GetGamepadName(gp), id);
                }
            }
        }

        for (auto it = _gamepads.begin(); it != _gamepads.end(); )
        {
            const bool stillConnected = std::ranges::find(currentIDs,
                                                    it->first) != currentIDs.end();
            if (!stillConnected)
            {
                LOG_INFO(Platform, "Gamepad disconnected (id={})", it->first);
                SDL_CloseGamepad(static_cast<SDL_Gamepad*>(it->second));
                it = _gamepads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    DetectDeviceFromMotion();

    // --- Evaluate actions ---------------------------------------------------
    // Compute IsPressed and AxisValue from current SDL state.
    // JustPressed / JustReleased are NOT set here — ProcessEdges() handles
    // those from the accumulated event queue, called once per game tick.
    for (auto& entry : _actions | std::views::values)
    {
        bool anyPressed  = false;
        f32  bestAxisVal = 0.0f;

        for (const Binding& b : entry.Bindings)
        {
            const f32 val = EvaluateBinding(b);
            if (std::abs(val) > std::abs(bestAxisVal))
                bestAxisVal = val;
            if (val != 0.0f)
                anyPressed = true;
        }

        entry.Pressed   = anyPressed;
        entry.AxisValue = bestAxisVal;
        // Reset edge flags — ProcessEdges() will set them correctly for each tick.
        entry.JustPressed  = false;
        entry.JustReleased = false;
    }
}

// ---------------------------------------------------------------------------
// ProcessEdges — called once per game tick inside the fixed-timestep loop
// ---------------------------------------------------------------------------

void InputManager::ProcessEdges()
{
    // Build per-button edge masks from the pending event sets.
    // Same bitmask convention as _mouseButtonMask (SDL_BUTTON_MASK = 1 << (sdlBtn-1)).
    _mouseJustDownMask = 0;
    _mouseJustUpMask   = 0;
    for (const i32 btn : _pendingMouseDown) _mouseJustDownMask |= SDL_BUTTON_MASK(btn);
    for (const i32 btn : _pendingMouseUp)   _mouseJustUpMask   |= SDL_BUTTON_MASK(btn);

    // Capture the first mouse button pressed this tick for the rebind screen.
    _anyJustPressedMouseButton = MouseButton::Count;
    if (!_pendingMouseDown.empty()) {
        const i32 sdlBtn = *_pendingMouseDown.begin();
        for (u32 i = 0; i < static_cast<u32>(MouseButton::Count); ++i) {
            if (ToSDLMouseButton(static_cast<MouseButton>(i)) == sdlBtn) {
                _anyJustPressedMouseButton = static_cast<MouseButton>(i);
                break;
            }
        }
    }

    // Publish accumulated scroll delta for this tick, then reset the accumulator.
    _scrollDelta        = _pendingScrollDelta;
    _pendingScrollDelta = 0.0f;

    // Publish accumulated text input for this tick (same pattern as scroll delta).
    _textInput = std::move(_pendingTextInput);
    _pendingTextInput.clear();

    // Capture the first raw key-down this tick for the rebind screen.
    _anyJustPressedKey = Key::Unknown;
    if (!_pendingKeyDown.empty()) {
        const SDL_Scancode sc = static_cast<SDL_Scancode>(*_pendingKeyDown.begin());
        for (u32 i = 0; i < static_cast<u32>(Key::Count); ++i) {
            if (ToScancode(static_cast<Key>(i)) == sc) {
                _anyJustPressedKey = static_cast<Key>(i);
                break;
            }
        }
    }

    // Capture the first gamepad button pressed this tick for the rebind screen.
    _anyJustPressedGamepadButton = GamepadButton::Count;
    if (!_pendingGamepadDown.empty()) {
        const i32 sdlBtn = _pendingGamepadDown.front().Button;
        for (u32 i = 0; i < static_cast<u32>(GamepadButton::Count); ++i) {
            if (static_cast<i32>(ToSDLGamepadButton(static_cast<GamepadButton>(i))) == sdlBtn) {
                _anyJustPressedGamepadButton = static_cast<GamepadButton>(i);
                break;
            }
        }
    }

    // Just-released tracking (rebind screen listens on release to avoid capturing
    // the mouse-up that first opened the listening dialog).
    _anyJustReleasedKey = Key::Unknown;
    if (!_pendingKeyUp.empty()) {
        const SDL_Scancode sc = static_cast<SDL_Scancode>(*_pendingKeyUp.begin());
        for (u32 i = 0; i < static_cast<u32>(Key::Count); ++i) {
            if (ToScancode(static_cast<Key>(i)) == sc) {
                _anyJustReleasedKey = static_cast<Key>(i);
                break;
            }
        }
    }

    _anyJustReleasedMouseButton = MouseButton::Count;
    if (!_pendingMouseUp.empty()) {
        const i32 sdlBtn = *_pendingMouseUp.begin();
        for (u32 i = 0; i < static_cast<u32>(MouseButton::Count); ++i) {
            if (ToSDLMouseButton(static_cast<MouseButton>(i)) == sdlBtn) {
                _anyJustReleasedMouseButton = static_cast<MouseButton>(i);
                break;
            }
        }
    }

    _anyJustReleasedGamepadButton = GamepadButton::Count;
    if (!_pendingGamepadUp.empty()) {
        const i32 sdlBtn = _pendingGamepadUp.front().Button;
        for (u32 i = 0; i < static_cast<u32>(GamepadButton::Count); ++i) {
            if (static_cast<i32>(ToSDLGamepadButton(static_cast<GamepadButton>(i))) == sdlBtn) {
                _anyJustReleasedGamepadButton = static_cast<GamepadButton>(i);
                break;
            }
        }
    }

    for (auto& entry : _actions | std::views::values)
    {
        bool anyJustPressed  = false;
        bool anyJustReleased = false;

        for (const Binding& b : entry.Bindings)
        {
            switch (b.BindingType)
            {
                case Binding::Type::Key:
                {
                    const i32 sc = static_cast<i32>(ToScancode(b.BoundKey));
                    if (_pendingKeyDown.contains(sc)) anyJustPressed  = true;
                    if (_pendingKeyUp.contains(sc))   anyJustReleased = true;
                    break;
                }
                case Binding::Type::MouseButton:
                {
                    const i32 btn = ToSDLMouseButton(b.BoundMouse);
                    if (_pendingMouseDown.contains(btn)) anyJustPressed  = true;
                    if (_pendingMouseUp.contains(btn))   anyJustReleased = true;
                    break;
                }
                case Binding::Type::GamepadButton:
                {
                    const i32 sdlBtn = static_cast<i32>(ToSDLGamepadButton(b.BoundButton));
                    for (const auto& [id, btn] : _pendingGamepadDown)
                        if (btn == sdlBtn) { anyJustPressed = true; break; }
                    for (const auto& [id, btn] : _pendingGamepadUp)
                        if (btn == sdlBtn) { anyJustReleased = true; break; }
                    break;
                }
                default:
                    break;
            }
        }

        entry.JustPressed  = anyJustPressed;
        // JustReleased is only meaningful when the action is no longer pressed.
        entry.JustReleased = anyJustReleased && !entry.Pressed;
    }

    _pendingKeyDown.clear();
    _pendingKeyUp.clear();
    _pendingMouseDown.clear();
    _pendingMouseUp.clear();
    _pendingGamepadDown.clear();
    _pendingGamepadUp.clear();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool InputManager::IsPressed(const ActionID action) const
{
    if (_typing && !_keepWhileTyping.contains(action)) return false;
    const auto it = _actions.find(action);
    return it != _actions.end() && it->second.Pressed;
}

bool InputManager::JustPressed(const ActionID action) const
{
    if (_typing && !_keepWhileTyping.contains(action)) return false;
    const auto it = _actions.find(action);
    return it != _actions.end() && it->second.JustPressed;
}

bool InputManager::JustReleased(const ActionID action) const
{
    if (_typing && !_keepWhileTyping.contains(action)) return false;
    const auto it = _actions.find(action);
    return it != _actions.end() && it->second.JustReleased;
}

f32 InputManager::AxisValue(const ActionID action) const
{
    if (_typing && !_keepWhileTyping.contains(action)) return 0.0f;
    const auto it = _actions.find(action);
    return it != _actions.end() ? it->second.AxisValue : 0.0f;
}

void InputManager::GetMousePosition(i32& outX, i32& outY) const
{
    outX = _mouseX;
    outY = _mouseY;
}

void InputManager::GetMouseDelta(i32& outDx, i32& outDy) const
{
    outDx = _mouseX - _prevMouseX;
    outDy = _mouseY - _prevMouseY;
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

std::string_view InputManager::GetActionName(const ActionID action) const
{
    const auto it = _actions.find(action);
    return it != _actions.end() ? std::string_view(it->second.Name) : std::string_view{};
}

std::string_view InputManager::GetActionDisplayName(const ActionID action) const
{
    const auto it = _actions.find(action);
    return it != _actions.end() ? std::string_view(it->second.DisplayName) : std::string_view{};
}

std::string_view InputManager::GetActionCategory(const ActionID action) const
{
    const auto it = _actions.find(action);
    return it != _actions.end() ? std::string_view(it->second.Category) : std::string_view{};
}

void InputManager::UnbindKey(const Key key)
{
    for (auto& [id, entry] : _actions)
        std::erase_if(entry.Bindings, [key](const Binding& b) {
            return b.BindingType == Binding::Type::Key && b.BoundKey == key;
        });
}

void InputManager::AddBinding(const ActionID action, const Binding& b)
{
    GetOrCreate(action).Bindings.push_back(b);
}

void InputManager::RemoveBinding(const ActionID action, const Binding& b)
{
    if (const auto it = _actions.find(action); it != _actions.end())
        std::erase_if(it->second.Bindings, [&b](const Binding& x) { return x == b; });
}

void InputManager::ClearKBMBindings(const ActionID action)
{
    if (const auto it = _actions.find(action); it != _actions.end())
        std::erase_if(it->second.Bindings, [](const Binding& b) {
            return b.BindingType == Binding::Type::Key ||
                   b.BindingType == Binding::Type::MouseButton;
        });
}

void InputManager::ClearGamepadBindings(const ActionID action)
{
    if (const auto it = _actions.find(action); it != _actions.end())
        std::erase_if(it->second.Bindings, [](const Binding& b) {
            return b.BindingType == Binding::Type::GamepadButton;
        });
}

void InputManager::UnbindMouseButton(const MouseButton button)
{
    for (auto& [id, entry] : _actions)
        std::erase_if(entry.Bindings, [button](const Binding& b) {
            return b.BindingType == Binding::Type::MouseButton && b.BoundMouse == button;
        });
}

void InputManager::UnbindGamepadButton(const GamepadButton button)
{
    for (auto& [id, entry] : _actions)
        std::erase_if(entry.Bindings, [button](const Binding& b) {
            return b.BindingType == Binding::Type::GamepadButton && b.BoundButton == button;
        });
}

const std::vector<Binding>& InputManager::GetBindings(const ActionID action) const
{
    const auto it = _actions.find(action);
    return it != _actions.end() ? it->second.Bindings : s_EmptyBindings;
}

std::vector<ActionID> InputManager::GetAllActions() const
{
    std::vector<ActionID> result;
    result.reserve(_actions.size());
    for (const auto& [id, _] : _actions)
        result.push_back(id);
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

InputManager::ActionEntry& InputManager::GetOrCreate(const ActionID action)
{
    return _actions[action]; // default-constructs if not present
}

f32 InputManager::EvaluateBinding(const Binding& binding) const
{
    switch (binding.BindingType)
    {
        // ----- Keyboard -----------------------------------------------------
        case Binding::Type::Key:
        {
            if (!_keyState) return 0.0f;
            const SDL_Scancode sc = ToScancode(binding.BoundKey);
            return (sc != SDL_SCANCODE_UNKNOWN && _keyState[sc]) ? 1.0f : 0.0f;
        }

        // ----- Mouse button -------------------------------------------------
        case Binding::Type::MouseButton:
        {
            const i32 sdlBtn = ToSDLMouseButton(binding.BoundMouse);
            return (_mouseButtonMask & SDL_BUTTON_MASK(sdlBtn)) ? 1.0f : 0.0f;
        }

        // ----- Gamepad button -----------------------------------------------
        case Binding::Type::GamepadButton:
        {
            const SDL_GamepadButton sdlBtn = ToSDLGamepadButton(binding.BoundButton);
            for (const auto& handle : _gamepads | std::views::values)
            {
                if (SDL_GetGamepadButton(static_cast<SDL_Gamepad*>(handle), sdlBtn))
                    return 1.0f;
            }
            return 0.0f;
        }

        // ----- Gamepad axis -------------------------------------------------
        case Binding::Type::GamepadAxis:
        {
            const SDL_GamepadAxis sdlAxis = ToSDLGamepadAxis(binding.BoundAxis);
            f32 best = 0.0f;
            for (const auto& handle : _gamepads | std::views::values)
            {
                const i16 raw  = SDL_GetGamepadAxis(static_cast<SDL_Gamepad*>(handle), sdlAxis);
                const f32 value = binding.ShapeAxis(NormalizeAxis(raw));
                if (std::abs(value) > std::abs(best))
                    best = value;
            }
            return best;
        }
    }

    return 0.0f;
}

void InputManager::InjectTextInput(const char* utf8)
{
    if (utf8) _pendingTextInput += utf8;
}

} // namespace Arcbit
