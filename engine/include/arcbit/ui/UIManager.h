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

    // Tab-only focus navigation — active even when a text widget is focused.
    static constexpr ActionID ActionTabNext  = MakeAction("UI_TabNext");

    // Back / cancel — Escape on keyboard, East face button (B/Circle) on gamepad.
    // Fires UIScreen::OnBackPressed() when no text widget has focus.
    static constexpr ActionID ActionBack     = MakeAction("UI_Back");

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

    Vec2 _mousePos      = {0, 0};
    bool _mouseDown     = false;
    bool _mouseJustDown = false;
    bool _mouseJustUp   = false;

    std::function<void(bool)> _textInputActiveCallback;
    bool _textInputActive = false; // tracks current SDL text input mode

    // Returns the topmost screen that is not fading out (receives input).
    [[nodiscard]] UIScreen* ActiveInputScreen() const;

    // Remove screens that have completed their fade-out.
    void RemoveCompletedFadeOuts();
};

} // namespace Arcbit
