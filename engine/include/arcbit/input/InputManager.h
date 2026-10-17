#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/core/Types.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// InputManager
//
// Maps physical inputs (keys, mouse buttons, gamepad buttons/axes) to named
// actions, then provides per-frame pressed/just-pressed/just-released queries.
//
// Workflow
// --------
//   1. At startup, register action names and add bindings:
//
//        namespace Actions {
//            constexpr ActionID Jump     = MakeAction("Jump");
//            constexpr ActionID MoveLeft = MakeAction("Move_Left");
//        }
//        input.RegisterAction(Actions::Jump,     "Jump");
//        input.RegisterAction(Actions::MoveLeft, "Move_Left");
//        input.BindKey(Actions::Jump,     Key::Space);
//        input.BindGamepadButton(Actions::Jump, GamepadButton::South);
//        input.BindKey(Actions::MoveLeft, Key::A);
//        input.BindKey(Actions::MoveLeft, Key::Left);
//
//   2. Each frame, call Update() AFTER Window::PollEvents(), then
//      ProcessEdges() once per game tick inside the fixed-timestep loop:
//
//        window.PollEvents();   // routes key/mouse/gamepad events via callbacks
//        input.Update();        // polls current pressed state
//        // inside fixed-timestep loop:
//        input.ProcessEdges();  // consumes events → sets JustPressed/JustReleased
//        input.OnUpdate();      // game logic reads JustPressed here
//
//   3. Query actions anywhere in game logic:
//
//        if (input.JustPressed(Actions::Jump))   { player.StartJump(); }
//        f32 dx = input.AxisValue(Actions::MoveLeft);
//
// Thread safety
// -------------
//   NOT thread-safe. Call Update()/ProcessEdges() and all query methods from
//   the same thread (the main/game thread).
//
// Gamepad support
// ---------------
//   InputManager automatically opens newly connected gamepads and closes
//   disconnected ones each time Update() is called — no event handling needed
//   in game code. All connected gamepads are OR'd together: if any gamepad has
//   a binding active, the action is considered pressed.
// ---------------------------------------------------------------------------
class InputManager
{
public:
    InputManager();
    ~InputManager();

    // InputManager is non-copyable (owns SDL_Gamepad handles).
    InputManager(const InputManager&)            = delete;
    InputManager& operator=(const InputManager&) = delete;

    // -----------------------------------------------------------------------
    // Registration
    //
    // RegisterAction links a human-readable name to an ActionID for
    // serialization (Settings System, Lua). Calling it is optional — actions
    // work without names, but the settings screen needs them to display and
    // save bindings.
    // -----------------------------------------------------------------------
    void RegisterAction(ActionID action, std::string_view name,
                        std::string_view displayName = {},
                        std::string_view category    = {});

    // -----------------------------------------------------------------------
    // Binding
    //
    // Multiple bindings can be added per action — the action is pressed if
    // ANY binding is active. Call ClearBindings first when rebinding in the
    // settings screen to replace rather than accumulate.
    // -----------------------------------------------------------------------
    void BindKey          (ActionID action, Key           key);
    void BindMouseButton  (ActionID action, MouseButton   button);
    void BindGamepadButton(ActionID action, GamepadButton button);
    void BindGamepadAxis  (ActionID action, GamepadAxis   axis, AxisHalf half = AxisHalf::Full,
                           f32 deadzone = 0.15f);

    // Remove all bindings for an action without unregistering it.
    void ClearBindings(ActionID action);

    // Remove all key bindings for a specific key across every action.
    void UnbindKey(Key key);

    // Remove all key/mouse bindings for a single action (leaves gamepad intact).
    void ClearKBMBindings(ActionID action);

    // Remove all gamepad-button bindings for a single action (leaves keys/mouse intact).
    void ClearGamepadBindings(ActionID action);

    // Remove a specific mouse button from every action.
    void UnbindMouseButton(MouseButton button);

    // Remove a specific gamepad button from every action.
    void UnbindGamepadButton(GamepadButton button);

    // -----------------------------------------------------------------------
    // Input event injection
    //
    // Called by Application to forward raw SDL events captured during
    // Window::PollEvents(). Parameters are opaque ints matching the SDL types
    // (SDL_Scancode, SDL button index, SDL_GamepadButton) without requiring
    // SDL headers here.
    //
    // Events accumulate in pending sets between ProcessEdges() calls so they
    // are never lost if the fixed-timestep loop fires zero times in a frame.
    // -----------------------------------------------------------------------
    void InjectKeyEvent     (i32 scancode,   bool down);
    void InjectMouseButton  (i32 sdlButton,  bool down);
    void InjectGamepadButton(u32 joystickId, i32 sdlButton, bool down);

    // -----------------------------------------------------------------------
    // Per-frame update
    //
    // Must be called once per display frame, AFTER Window::PollEvents().
    // Polls current keyboard/mouse/gamepad state for IsPressed and AxisValue.
    // Does NOT set JustPressed/JustReleased — call ProcessEdges() for those.
    // -----------------------------------------------------------------------
    void Update();

    // -----------------------------------------------------------------------
    // Per-tick edge processing
    //
    // Call once per game tick (inside the fixed-timestep loop), after Update().
    // Consumes accumulated key/mouse/gamepad button events and sets
    // JustPressed / JustReleased for all actions, then clears the event queue.
    //
    // Why separate from Update()?
    //   Update() runs every display frame; the fixed-timestep OnUpdate() may
    //   run 0 or N times per display frame. Putting edge detection here ensures
    //   events are held until a tick consumes them — no JustPressed is ever lost
    //   because the accumulator was slightly short of the threshold.
    // -----------------------------------------------------------------------
    void ProcessEdges();

    // -----------------------------------------------------------------------
    // Action queries
    // -----------------------------------------------------------------------

    // True every frame that at least one binding for this action is held.
    [[nodiscard]] bool IsPressed    (ActionID action) const;

    // True for exactly ONE tick — the tick the action first becomes pressed.
    [[nodiscard]] bool JustPressed  (ActionID action) const;

    // True for exactly ONE tick — the tick all bindings are released.
    [[nodiscard]] bool JustReleased (ActionID action) const;

    // Analogue value in [-1, 1] (axes) or 0/1 (buttons/keys).
    // For actions with multiple bindings, returns the value with the largest
    // absolute magnitude.
    [[nodiscard]] f32  AxisValue    (ActionID action) const;

    // -----------------------------------------------------------------------
    // Mouse queries
    // -----------------------------------------------------------------------

    // Absolute cursor position in window pixels (top-left origin).
    void GetMousePosition(i32& outX, i32& outY) const;

    // Pixels the cursor moved since the previous Update() call.
    void GetMouseDelta(i32& outDx, i32& outDy) const;

    // True if the given mouse button is currently held down.
    // Valid after Update() each display frame.
    [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const
    {
        return (_mouseButtonMask & (1u << static_cast<u32>(button))) != 0;
    }

    // True if the button transitioned down/up this game tick.
    // Valid after ProcessEdges() — read these in OnUpdate / UIManager::Update.
    [[nodiscard]] bool IsMouseButtonJustDown(MouseButton button) const
    {
        return (_mouseJustDownMask & (1u << static_cast<u32>(button))) != 0;
    }

    [[nodiscard]] bool IsMouseButtonJustUp(MouseButton button) const
    {
        return (_mouseJustUpMask & (1u << static_cast<u32>(button))) != 0;
    }

    // -----------------------------------------------------------------------
    // Serialization helpers (consumed by Settings System in Phase 10)
    // -----------------------------------------------------------------------

    // Human-readable name registered for this action, or "" if unregistered.
    [[nodiscard]] std::string_view GetActionName(ActionID action)        const;
    [[nodiscard]] std::string_view GetActionDisplayName(ActionID action) const;
    [[nodiscard]] std::string_view GetActionCategory(ActionID action)    const;

    // Read-only view of all bindings for an action (for serialization/display).
    [[nodiscard]] const std::vector<Binding>& GetBindings(ActionID action) const;

    // All registered action IDs — used by the Settings System to enumerate
    // actions when saving/loading the input binding file.
    [[nodiscard]] std::vector<ActionID> GetAllActions() const;

    // Returns the first key pressed this tick (from ProcessEdges), or Key::Unknown.
    [[nodiscard]] Key GetAnyJustPressedKey() const { return _anyJustPressedKey; }

    // Returns the first mouse button pressed this tick, or MouseButton::Count if none.
    [[nodiscard]] MouseButton GetAnyJustPressedMouseButton() const { return _anyJustPressedMouseButton; }

    // Returns the first gamepad button pressed this tick, or GamepadButton::Count if none.
    [[nodiscard]] GamepadButton GetAnyJustPressedGamepadButton() const { return _anyJustPressedGamepadButton; }

    // Released equivalents — use these in the rebind screen to avoid capturing the
    // click that opens listening mode (click fires on mouse-up; we then wait for
    // the next full press+release cycle before considering input committed).
    [[nodiscard]] Key         GetAnyJustReleasedKey()            const { return _anyJustReleasedKey; }
    [[nodiscard]] MouseButton GetAnyJustReleasedMouseButton()    const { return _anyJustReleasedMouseButton; }
    [[nodiscard]] GamepadButton GetAnyJustReleasedGamepadButton() const { return _anyJustReleasedGamepadButton; }

    // Add or remove a specific binding from an action.
    // Used by InputRebindScreen to handle chips (add-on-release, remove-on-click,
    // restore-on-cancel).
    void AddBinding   (ActionID action, const Binding& b);
    void RemoveBinding(ActionID action, const Binding& b);

    // Accumulated mouse wheel delta this tick (positive = scroll up/away from user).
    // ProcessEdges moves the pending value into the readable slot each tick.
    void InjectMouseScroll(f32 delta)
    {
        _pendingScrollDelta += delta;
        NoteDevice(InputDevice::KeyboardMouse);
    }
    [[nodiscard]] f32 GetScrollDelta() const { return _scrollDelta; }

    // The kind of device the player used last: a key, mouse button, scroll or
    // real mouse movement means keyboard / mouse; a gamepad button or a stick
    // or trigger pushed past half way means gamepad. Starts as keyboard / mouse.
    [[nodiscard]] InputDevice GetLastDevice() const { return _lastDevice; }

    // True once after the last-used device changed kind (Application turns
    // it into engine.input.device_changed).
    [[nodiscard]] bool ConsumeDeviceChanged() { return std::exchange(_deviceChanged, false); }

    // UTF-8 text typed this tick (composed, IME-aware). Populated from
    // SDL_EVENT_TEXT_INPUT via InjectTextInput; available after ProcessEdges().
    void InjectTextInput(const char* utf8);
    [[nodiscard]] const std::string& GetTextInput() const { return _textInput; }

    // -----------------------------------------------------------------------
    // Display helpers
    // -----------------------------------------------------------------------

    // Short human-readable name for a key: "A", "Space", "F1", "LShift", etc.
    [[nodiscard]] static std::string_view KeyToString(Key key);

    // Human-readable description of a binding: "A", "Space", "Ctrl A", etc.
    [[nodiscard]] static std::string BindingToString(const Binding& b);

private:
    // Record the kind of device just used; flags a change for ConsumeDeviceChanged.
    void NoteDevice(InputDevice device)
    {
        if (device == _lastDevice) return;
        _lastDevice    = device;
        _deviceChanged = true;
    }

    // Mouse movement and stick / trigger pushes - device use without an event.
    void DetectDeviceFromMotion();
    // Per-action data stored in the registry.
    struct ActionEntry
    {
        std::string Name;
        std::string DisplayName; // human-friendly label for UI, e.g. "Toggle Grid"
        std::string Category;    // grouping label for UI, e.g. "Debug"
        std::vector<Binding> Bindings;

        // State computed each frame.
        bool Pressed      = false;
        bool JustPressed  = false;
        bool JustReleased = false;
        f32  AxisValue    = 0.0f;
    };

    // Returns or creates an entry for the given action ID.
    ActionEntry& GetOrCreate(ActionID action);

    // Evaluate whether a single binding is active this frame. Returns the
    // analogue value (0/1 for digital, -1..1 for axes).
    [[nodiscard]] f32 EvaluateBinding(const Binding& binding) const;

    // --- SDL state (populated by Update) ------------------------------------

    // SDL keyboard state array — pointer into SDL's internal buffer, valid
    // until the next SDL_PollEvent / SDL_PumpEvents call.
    const bool* _keyState = nullptr;

    // SDL mouse state bitmask and position.
    u32 _mouseButtonMask   = 0;
    u32 _mouseJustDownMask = 0;  // set by ProcessEdges(), cleared at next ProcessEdges() call
    u32 _mouseJustUpMask   = 0;
    Key           _anyJustPressedKey             = Key::Unknown;
    MouseButton   _anyJustPressedMouseButton     = MouseButton::Count;
    GamepadButton _anyJustPressedGamepadButton   = GamepadButton::Count;
    Key           _anyJustReleasedKey            = Key::Unknown;
    MouseButton   _anyJustReleasedMouseButton    = MouseButton::Count;
    GamepadButton _anyJustReleasedGamepadButton  = GamepadButton::Count;
    f32           _pendingScrollDelta            = 0.0f;
    InputDevice   _lastDevice                    = InputDevice::KeyboardMouse;
    bool          _deviceChanged                 = false;
    bool          _mouseSeen                     = false; // Update has read the mouse once (no jump from 0,0)
    u32           _axesPushed                    = 0;     // gamepad axes past half way last Update (a bit each)
    f32           _scrollDelta                   = 0.0f;
    std::string   _pendingTextInput;  // accumulates between ProcessEdges calls
    std::string   _textInput;         // available to read after ProcessEdges
    i32 _mouseX = 0, _mouseY = 0;
    i32 _prevMouseX = 0, _prevMouseY = 0;

    // Open gamepad handles. Keys are SDL_JoystickID (opaque u32 in SDL3).
    // Stored as void* to keep SDL3/SDL.h out of this header.
    std::unordered_map<u32, void*> _gamepads; // SDL_JoystickID → SDL_Gamepad*

    // --- Action registry ----------------------------------------------------
    std::unordered_map<ActionID, ActionEntry> _actions;

    // Returned by GetBindings when the action has no entry, so we never
    // return a dangling reference.
    static const std::vector<Binding> s_EmptyBindings;

    // --- Pending input events (accumulated between ProcessEdges() calls) ----
    // Populated by InjectKeyEvent / InjectMouseButton / InjectGamepadButton.
    // ProcessEdges() reads these to set JustPressed/JustReleased, then clears them.

    struct PendingGamepadEvent { u32 JoystickId; i32 Button; };

    std::unordered_set<i32>           _pendingKeyDown;
    std::unordered_set<i32>           _pendingKeyUp;
    std::unordered_set<i32>           _pendingMouseDown;
    std::unordered_set<i32>           _pendingMouseUp;
    std::vector<PendingGamepadEvent>  _pendingGamepadDown;
    std::vector<PendingGamepadEvent>  _pendingGamepadUp;
};

} // namespace Arcbit
