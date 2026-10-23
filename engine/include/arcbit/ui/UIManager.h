#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/input/InputTypes.h>

#include <functional>
#include <memory>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  InputManager;
class  RenderThread;

// ---------------------------------------------------------------------------
// UIManager — owns the screen stack, drives update/collect each frame.
//
// Push screens to show them; Pop starts a fade-out before removal.
// All visible screens are collected (rendered); only the topmost non-fading
// screen receives input.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// NavRepeat - held focus navigation. A press moves focus once; held past
// Delay, it keeps moving every Interval until released - like holding an
// arrow key in a text editor. Fed each tick with the next / previous
// direction's press edges and held state; returns the step to take
// (+1 next, -1 previous, 0 none). The newest press wins.
// ---------------------------------------------------------------------------
struct NavRepeat
{
    f32 Delay    = 0.4f;  // seconds held before repeating starts
    f32 Interval = 0.08f; // seconds between repeated steps

    i32 Step(bool justNext, bool justPrev, bool heldNext, bool heldPrev, f32 dt);

private:
    i32 _held  = 0; // the direction being held: +1, -1, or 0
    f32 _timer = 0.0f;
};

class UIManager
{
public:
    // Pre-registered UI navigation actions — bound to defaults in Init().
    // Games may rebind these via Settings the same as any other action.
    static constexpr ActionID ActionConfirm   = MakeAction("UI_Confirm");
    static constexpr ActionID ActionFocusNext = MakeAction("UI_FocusNext");
    static constexpr ActionID ActionFocusPrev = MakeAction("UI_FocusPrev");

    // Text-widget control keys — hidden from the rebind screen (UI_ prefix).
    static constexpr ActionID ActionTextLeft      = MakeAction("UI_TextLeft");
    static constexpr ActionID ActionTextRight     = MakeAction("UI_TextRight");
    static constexpr ActionID ActionTextHome      = MakeAction("UI_TextHome");
    static constexpr ActionID ActionTextEnd       = MakeAction("UI_TextEnd");
    static constexpr ActionID ActionTextBackspace = MakeAction("UI_TextBackspace");
    static constexpr ActionID ActionTextDelete    = MakeAction("UI_TextDelete");
    static constexpr ActionID ActionTextEscape    = MakeAction("UI_TextEscape");
    // With Ctrl held: select all, copy, cut, paste in a text field.
    static constexpr ActionID ActionTextSelectAll = MakeAction("UI_TextSelectAll");
    static constexpr ActionID ActionTextCopy      = MakeAction("UI_TextCopy");
    static constexpr ActionID ActionTextCut       = MakeAction("UI_TextCut");
    static constexpr ActionID ActionTextPaste     = MakeAction("UI_TextPaste");

    // Tab-only focus navigation — active even when a text widget is focused.
    static constexpr ActionID ActionTabNext  = MakeAction("UI_TabNext");

    // Back / cancel — Escape on keyboard, East face button (B/Circle) on gamepad.
    // Fires UIScreen::OnBackPressed() when no text widget has focus.
    static constexpr ActionID ActionBack     = MakeAction("UI_Back");

    // Gamepad pointer: the left stick moves a cursor over menus and South
    // clicks what's under it; the right stick scrolls (see UpdatePointer).
    static constexpr ActionID ActionCursorLeft  = MakeAction("UI_CursorLeft");
    static constexpr ActionID ActionCursorRight = MakeAction("UI_CursorRight");
    static constexpr ActionID ActionCursorUp    = MakeAction("UI_CursorUp");
    static constexpr ActionID ActionCursorDown  = MakeAction("UI_CursorDown");
    static constexpr ActionID ActionCursorClick = MakeAction("UI_CursorClick");
    static constexpr ActionID ActionScrollUp    = MakeAction("UI_ScrollUp");
    static constexpr ActionID ActionScrollDown  = MakeAction("UI_ScrollDown");

    // Drag and drop by keys (docs/gui.md#drag-and-drop): pick up from / put
    // down on the focused widget (Space, gamepad West); turn what's held (R,
    // gamepad North).
    static constexpr ActionID ActionGrab   = MakeAction("UI_Grab");
    static constexpr ActionID ActionRotate = MakeAction("UI_Rotate");

    // One direction each (arrows, D-pad), for moving through a GridPanel's
    // cells; FocusNext / FocusPrev fold them into a line elsewhere.
    static constexpr ActionID ActionNavLeft  = MakeAction("UI_NavLeft");
    static constexpr ActionID ActionNavRight = MakeAction("UI_NavRight");
    static constexpr ActionID ActionNavUp    = MakeAction("UI_NavUp");
    static constexpr ActionID ActionNavDown  = MakeAction("UI_NavDown");

    // Modifier state — used to dispatch Shift/Ctrl variants of control keys.
    static constexpr ActionID ActionShiftMod = MakeAction("UI_ShiftMod");
    static constexpr ActionID ActionCtrlMod  = MakeAction("UI_CtrlMod");

    // Must be called once after the render device is ready.
    // Registers navigation actions and binds their default keys.
    void Init(const RenderThread& rt, const FontAtlas& font, InputManager& input);

    // Preserves the current font pointer when the incoming skin has none,
    // since JSON skins cannot serialize FontAtlas pointers.
    void SetSkin(const UISkin& skin)
    {
        const FontAtlas* prev = _skin.Font;
        _skin = skin;
        if (!_skin.Font) _skin.Font = prev;
    }
    [[nodiscard]] const UISkin& GetSkin() const { return _skin; }

    // Push a screen on top of its layer (UIScreen::Layer): above the screens
    // of its own and lower layers, beneath those of higher ones. Settings
    // screens a menu opens are pushed this way, on top of it.
    void Push(std::unique_ptr<UIScreen> screen);

    // Open a top-level menu: close every open Menu-layer screen (another
    // menu and its sub-screens), then push this one. The pause menu, the
    // title menu and a game's own menus open this way, so opening one swaps
    // out whichever was open.
    void OpenMenu(std::unique_ptr<UIScreen> screen);

    // Close every open Menu-layer screen.
    void CloseMenus();

    // Close the topmost Menu-layer screen (fading out if it has a
    // transition). Prefer Close(screen): a screen closing itself by pointer
    // can't close the wrong one.
    void Pop();

    // Close a specific screen, wherever it sits in the stack - fading out if
    // it has a transition, like Pop. No-op if the screen isn't on the stack.
    void Close(const UIScreen* screen);

    // Remove a specific screen immediately, wherever it sits in the stack (no
    // fade). For screens owned by something going away — a scene's HUD when
    // the scene unloads. No-op if the screen isn't on the stack.
    //
    // Called during Update (from a screen's own callback - a splash removing
    // itself when it finishes), the removal waits until Update returns, so
    // the screen isn't destroyed while it's still being ticked. Pop() of a
    // screen without a transition is deferred the same way.
    void Remove(const UIScreen* screen);

    [[nodiscard]] UIScreen* Top()   const;
    [[nodiscard]] bool      Empty() const { return _stack.empty(); }

    // True while the screen is on the stack and not closing.
    [[nodiscard]] bool IsOpen(const UIScreen* screen) const;

    // True while any Menu-layer screen is open (and not closing).
    [[nodiscard]] bool HasOpenMenu() const;

    // The UI's back input this tick (Escape, gamepad East). A key that also
    // opens a menu (Escape for pause) should be read as "back" while a menu
    // is open, so it closes that menu instead of swapping in its own.
    [[nodiscard]] static bool IsBackPressed(const InputManager& input);

    // Returns true if any screen on the stack has BlocksInput = true.
    // Use this in OnUpdate to suppress game input when a menu is open.
    [[nodiscard]] bool HasBlockingScreen() const;

    // Returns true if any screen on the stack has BlocksGame = true.
    // Application uses this to suppress OnUpdate, OnRender, and scene rendering.
    [[nodiscard]] bool HasGameBlockingScreen() const;

    // True when every game-blocking screen on the stack covers loading (see
    // UIScreen::CoversLoading): a scene may load beneath them now.
    [[nodiscard]] bool CoversLoading() const;

    // Called with true when a text-consuming widget gains focus, false when it
    // loses focus. Wire to Window::SetTextInputActive so SDL text input mode is
    // only active while the user is typing — prevents TSF on Windows from
    // intercepting Enter and blocking it from reaching SDL_EVENT_KEY_DOWN.
    void SetTextInputActiveCallback(std::function<void(bool)> fn)
    {
        _textInputActiveCallback = std::move(fn);
    }

    void Update(f32 dt, Vec2 windowSize, const InputManager& input);
    void CollectRenderData(FramePacket& packet, Vec2 windowSize);

private:
    std::vector<std::unique_ptr<UIScreen>> _stack;
    std::vector<const UIScreen*>           _deferredRemovals; // Remove / Pop called during Update
    bool                                   _updating = false;

    // Update's body; Update wraps it to run deferred removals afterwards.
    void UpdateScreens(f32 dt, Vec2 windowSize, const InputManager& input);
    UISkin        _skin;
    TextureHandle _whiteTex;
    SamplerHandle _whiteSampler;

    Vec2 _mousePos      = {0, 0}; // the pointer: the mouse, or the gamepad cursor while it's shown
    bool _mouseDown     = false;
    bool _mouseJustDown = false;
    bool _mouseJustUp   = false;
    bool _altDown       = false;  // the right button: the alternate drag
    bool _altJustDown   = false;
    bool _altJustUp     = false;
    f32  _scrollDelta   = 0.0f;   // wheel plus right stick

    // The gamepad cursor: shown once the left stick moves while a screen
    // takes input; hidden again by the mouse, keys or the D-pad.
    Vec2 _cursorPos    = {-1.0f, -1.0f}; // negative until first shown: starts mid-window
    bool _cursorActive = false;

    // Fill the pointer fields from the mouse or the gamepad cursor.
    void UpdatePointer(f32 dt, Vec2 windowSize, const InputManager& input);

    // The gamepad cursor, drawn above every screen while it's shown.
    void CollectCursor(FramePacket& packet) const;

    NavRepeat       _nav;
    NavRepeat       _navX, _navY;         // inside a GridPanel: across and down
    const UIScreen* _navScreen = nullptr; // the screen a held direction started on

    // Arrows / D-pad: focus along the line, or across a grid's cells.
    void MoveFocus(UIScreen& active, UIWidget* focused, bool consumesNav, const InputManager& input, f32 dt);
    // The grab / rotate / back keys on a drag, and screens that lost the input dropping theirs.
    [[nodiscard]] bool HandleDragKeys(UIScreen& active, const InputManager& input);
    static void RegisterDragActions(InputManager& input);

    std::function<void(bool)> _textInputActiveCallback;
    bool _textInputActive = false; // tracks current SDL text input mode
    void SyncTextInput(bool active); // report a change through the callback

    // Returns the topmost screen that is not fading out (receives input).
    [[nodiscard]] UIScreen* ActiveInputScreen() const;

    // Remove screens that have completed their fade-out.
    void RemoveCompletedFadeOuts();
};

} // namespace Arcbit
