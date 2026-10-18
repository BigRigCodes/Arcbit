#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UILoader.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <tuple>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Init — register nav actions and bind default keys/buttons.
// ---------------------------------------------------------------------------

void UIManager::Init(const RenderThread& rt, const FontAtlas& font, InputManager& input)
{
    _whiteTex     = rt.GetUIWhiteTexture();
    _whiteSampler = rt.GetUIWhiteSampler();
    _skin.Font    = &font;
    UILoader::RegisterFont("default", font);

    input.RegisterAction(ActionConfirm, "UI_Confirm");
    input.RegisterAction(ActionFocusNext, "UI_FocusNext");
    input.RegisterAction(ActionFocusPrev, "UI_FocusPrev");

    input.BindKey(ActionConfirm, Key::Enter);
    input.BindKey(ActionFocusNext, Key::Down);
    input.BindKey(ActionFocusNext, Key::Right);
    input.BindKey(ActionFocusPrev, Key::Up);
    input.BindKey(ActionFocusPrev, Key::Left);

    input.BindGamepadButton(ActionConfirm, GamepadButton::South);

    // The gamepad cursor and right-stick scrolling (UpdatePointer).
    for (const auto& [action, name, axis, half] : {
             std::tuple{ ActionCursorLeft, "UI_CursorLeft", GamepadAxis::LeftX, AxisHalf::Negative },
             std::tuple{ ActionCursorRight, "UI_CursorRight", GamepadAxis::LeftX, AxisHalf::Positive },
             std::tuple{ ActionCursorUp, "UI_CursorUp", GamepadAxis::LeftY, AxisHalf::Negative },
             std::tuple{ ActionCursorDown, "UI_CursorDown", GamepadAxis::LeftY, AxisHalf::Positive },
             std::tuple{ ActionScrollUp, "UI_ScrollUp", GamepadAxis::RightY, AxisHalf::Negative },
             std::tuple{ ActionScrollDown, "UI_ScrollDown", GamepadAxis::RightY, AxisHalf::Positive },
         }) {
        input.RegisterAction(action, name);
        input.BindGamepadAxis(action, axis, half, 0.2f);
    }
    input.RegisterAction(ActionCursorClick, "UI_CursorClick");
    input.BindGamepadButton(ActionCursorClick, GamepadButton::South);
    input.BindGamepadButton(ActionFocusNext, GamepadButton::DPadDown);
    input.BindGamepadButton(ActionFocusNext, GamepadButton::DPadRight);
    input.BindGamepadButton(ActionFocusPrev, GamepadButton::DPadUp);
    input.BindGamepadButton(ActionFocusPrev, GamepadButton::DPadLeft);

    // Text widget control keys — UI_ prefix hides them from the rebind screen.
    input.RegisterAction(ActionTextLeft, "UI_TextLeft");
    input.RegisterAction(ActionTextRight, "UI_TextRight");
    input.RegisterAction(ActionTextHome, "UI_TextHome");
    input.RegisterAction(ActionTextEnd, "UI_TextEnd");
    input.RegisterAction(ActionTextBackspace, "UI_TextBackspace");
    input.RegisterAction(ActionTextDelete, "UI_TextDelete");
    input.RegisterAction(ActionTextEscape, "UI_TextEscape");
    input.RegisterAction(ActionTabNext, "UI_TabNext");
    input.RegisterAction(ActionShiftMod, "UI_ShiftMod");
    input.RegisterAction(ActionCtrlMod, "UI_CtrlMod");
    input.RegisterAction(ActionBack, "UI_Back");

    input.BindKey(ActionTextLeft, Key::Left);
    input.BindKey(ActionTextRight, Key::Right);
    input.BindKey(ActionTextHome, Key::Home);
    input.BindKey(ActionTextEnd, Key::End);
    input.BindKey(ActionTextBackspace, Key::Backspace);
    input.BindKey(ActionTextDelete, Key::Delete);
    input.BindKey(ActionTextEscape, Key::Escape);
    input.BindKey(ActionTabNext, Key::Tab);
    input.BindKey(ActionShiftMod, Key::LeftShift);
    input.BindKey(ActionShiftMod, Key::RightShift);
    input.BindKey(ActionCtrlMod, Key::LeftCtrl);
    input.BindKey(ActionCtrlMod, Key::RightCtrl);

    input.BindGamepadButton(ActionBack, GamepadButton::East);

    // The UI drives text entry, so its keys keep working while someone types.
    for (const ActionID action : { ActionConfirm, ActionFocusNext, ActionFocusPrev, ActionCursorLeft, ActionCursorRight,
                                   ActionCursorUp, ActionCursorDown, ActionScrollUp, ActionScrollDown, ActionCursorClick,
                                   ActionTextLeft, ActionTextRight, ActionTextHome, ActionTextEnd, ActionTextBackspace,
                                   ActionTextDelete, ActionTextEscape, ActionTabNext, ActionShiftMod, ActionCtrlMod, ActionBack })
        input.KeepWhileTyping(action);
}

// ---------------------------------------------------------------------------
// Screen stack
// ---------------------------------------------------------------------------

void UIManager::Push(std::unique_ptr<UIScreen> screen)
{
    if (screen->TransitionSpeed > 0.0f) {
        screen->_transitionOpacity = 0.0f;
        screen->_transitionState   = UIScreen::TransitionState::FadingIn;
    }
    screen->_skipInputThisFrame = true;
    screen->OnEnter();

    // On top of its layer: before the first screen of a higher one (the stack
    // is kept sorted by layer).
    const UILayer layer = screen->Layer;
    const auto    at    = std::ranges::find_if(_stack, [layer](const auto& s) { return s->Layer > layer; });
    _stack.insert(at, std::move(screen));
}

void UIManager::OpenMenu(std::unique_ptr<UIScreen> screen)
{
    CloseMenus();
    screen->Layer = UILayer::Menu;
    Push(std::move(screen));
}

void UIManager::CloseMenus()
{
    std::vector<const UIScreen*> menus;
    for (const auto& s : _stack)
        if (s->Layer == UILayer::Menu && IsOpen(s.get())) menus.push_back(s.get());
    for (const UIScreen* menu : menus) Close(menu);
}

void UIManager::Pop()
{
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
        if ((*it)->Layer == UILayer::Menu && IsOpen(it->get())) {
            Close(it->get());
            return;
        }
}

bool UIManager::IsOpen(const UIScreen* screen) const
{
    if (std::ranges::find(_deferredRemovals, screen) != _deferredRemovals.end()) return false;
    const auto it = std::ranges::find_if(_stack, [screen](const auto& s) { return s.get() == screen; });
    return it != _stack.end() && !(*it)->IsFadingOut();
}

bool UIManager::HasOpenMenu() const
{
    return std::ranges::any_of(_stack, [this](const auto& s) { return s->Layer == UILayer::Menu && IsOpen(s.get()); });
}

bool UIManager::IsBackPressed(const InputManager& input)
{
    return input.JustPressed(ActionTextEscape) || input.JustPressed(ActionBack);
}

void UIManager::Close(const UIScreen* screen)
{
    const auto it = std::ranges::find_if(_stack, [&](const auto& s) { return s.get() == screen; });
    if (it == _stack.end()) return;

    UIScreen& target = **it;
    if (target.TransitionSpeed > 0.0f) {
        target._transitionOpacity = 1.0f;
        target._transitionState   = UIScreen::TransitionState::FadingOut;
    }
    else Remove(&target);
}

void UIManager::Remove(const UIScreen* screen)
{
    if (_updating) {
        if (std::ranges::find(_deferredRemovals, screen) == _deferredRemovals.end()) _deferredRemovals.push_back(screen);
        return;
    }
    const auto it = std::ranges::find_if(_stack, [&](const auto& s) { return s.get() == screen; });
    if (it == _stack.end()) return;
    (*it)->OnExit();
    _stack.erase(it);
}

UIScreen* UIManager::Top() const { return _stack.empty() ? nullptr : _stack.back().get(); }

bool UIManager::HasBlockingScreen() const
{
    for (const auto& s : _stack)
        if (s->BlocksInput) return true;
    return false;
}

bool UIManager::HasGameBlockingScreen() const
{
    for (const auto& s : _stack)
        if (s->BlocksGame && !s->IsFadingOut()) return true;
    return false;
}

bool UIManager::CoversLoading() const
{
    for (const auto& s : _stack)
        if (s->BlocksGame && !s->IsFadingOut() && !s->CoversLoading()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// NavRepeat
// ---------------------------------------------------------------------------

i32 NavRepeat::Step(const bool justNext, const bool justPrev, const bool heldNext, const bool heldPrev,
                    const f32 dt)
{
    if (justNext || justPrev) {
        _held  = justNext ? 1 : -1;
        _timer = Delay;
        return _held;
    }
    if (_held == 0 || !(_held > 0 ? heldNext : heldPrev)) {
        _held = 0;
        return 0;
    }
    _timer -= dt;
    if (_timer > 0.0f) return 0;
    _timer += Interval;
    return _held;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

// The topmost screen that takes input and isn't on its way out.
UIScreen* UIManager::ActiveInputScreen() const
{
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
        if ((*it)->TakesInput && !(*it)->IsFadingOut()) return it->get();
    return nullptr;
}

void UIManager::RemoveCompletedFadeOuts()
{
    for (auto it = _stack.begin(); it != _stack.end();) {
        if ((*it)->IsFadingOut() && (*it)->GetTransitionOpacity() <= 0.0f) {
            (*it)->OnExit();
            it = _stack.erase(it);
        }
        else { ++it; }
    }
}

void UIManager::Update(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    _updating = true;
    UpdateScreens(dt, windowSize, input);
    _updating = false;

    for (const UIScreen* screen : _deferredRemovals) Remove(screen);
    _deferredRemovals.clear();
}

// The pointer widgets see. The gamepad cursor takes over when the left stick
// moves while a screen takes input, and hands back as soon as the player
// uses the mouse, keyboard or D-pad (anything that makes the last-used device
// keyboard / mouse, or moves focus). While it's shown, South clicks under it.
void UIManager::UpdatePointer(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    constexpr f32 CursorSpeed = 1100.0f; // px/s at full tilt, for a 1080-high window
    constexpr f32 ScrollRate  = 10.0f;   // wheel notches per second at full tilt

    const Vec2 stick{ input.AxisValue(ActionCursorRight) - input.AxisValue(ActionCursorLeft),
                      input.AxisValue(ActionCursorDown) - input.AxisValue(ActionCursorUp) };
    const bool focusKeys = input.JustPressed(ActionFocusNext) || input.JustPressed(ActionFocusPrev);
    if (!ActiveInputScreen() || input.GetLastDevice() == InputDevice::KeyboardMouse || focusKeys)
        _cursorActive = false;
    else if (stick.X != 0.0f || stick.Y != 0.0f) {
        if (!_cursorActive && _cursorPos.X < 0.0f) _cursorPos = windowSize * 0.5f;
        _cursorActive = true;
        // Squared tilt: slow and precise near the center, fast at the edge.
        const f32 tilt = std::min(1.0f, std::sqrt(stick.X * stick.X + stick.Y * stick.Y));
        _cursorPos += stick * (CursorSpeed * tilt * dt * windowSize.Y / 1080.0f);
        _cursorPos  = { std::clamp(_cursorPos.X, 0.0f, windowSize.X), std::clamp(_cursorPos.Y, 0.0f, windowSize.Y) };
    }

    _scrollDelta = input.GetScrollDelta() +
                   (input.AxisValue(ActionScrollUp) - input.AxisValue(ActionScrollDown)) * ScrollRate * dt;
    if (_cursorActive) {
        _mousePos      = _cursorPos;
        _mouseDown     = input.IsPressed(ActionCursorClick);
        _mouseJustDown = input.JustPressed(ActionCursorClick);
        _mouseJustUp   = input.JustReleased(ActionCursorClick);
        return;
    }
    i32 mx = 0, my = 0;
    input.GetMousePosition(mx, my);
    _mousePos      = {static_cast<f32>(mx), static_cast<f32>(my)};
    _mouseDown     = input.IsMouseButtonDown(MouseButton::Left);
    _mouseJustDown = input.IsMouseButtonJustDown(MouseButton::Left);
    _mouseJustUp   = input.IsMouseButtonJustUp(MouseButton::Left);
}

void UIManager::UpdateScreens(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    for (const auto& s : _stack) s->AdvanceTransition(dt);
    RemoveCompletedFadeOuts();

    // Every screen refreshes, input or not (a snapshot: one may push another).
    std::vector<UIScreen*> screens;
    for (const auto& s : _stack) screens.push_back(s.get());
    for (UIScreen* s : screens) s->RefreshLocale(); // the language changed: text follows
    for (UIScreen* s : screens) s->OnUpdate(dt);

    UpdatePointer(dt, windowSize, input);

    UIScreen* active = ActiveInputScreen();
    if (!active) {
        SyncTextInput(false); // the screen being typed into just closed
        return;
    }

    // Screens set this flag on the frame they are pushed so that the key event
    // that caused the push (e.g. Escape to open a menu) is not also processed
    // as a back-press on the newly-visible screen. OnTick is skipped too: it
    // reads input directly, so a screen toggled by the same key (F1 opens, F1
    // closes) would otherwise close itself the tick it opened.
    const bool skipInput        = active->_skipInputThisFrame;
    active->_skipInputThisFrame = false;

    const UIRect screenRect = {0.0f, 0.0f, windowSize.X, windowSize.Y};
    if (!skipInput) active->OnTick(dt, input);
    active->Update(dt, screenRect, _mousePos, _mouseDown, _mouseJustDown, _mouseJustUp, _scrollDelta);

    if (skipInput) return;

    // Snapshot focus state before any navigation/back input runs. Used to:
    //  - gate arrow-key navigation (text-consuming widgets swallow arrows)
    //  - decide whether back-press cancels editing or pops the screen
    //  - detect focus changes for the focus-move sound below
    UIWidget* const prevFocused      = active->GetFocusedWidget();
    const bool      prevConsumesNav  = prevFocused && prevFocused->ConsumesFocusNav();

    // Arrow keys / D-pad, repeating while held. A direction held into a new
    // screen starts over rather than racing through it.
    if (active != _navScreen) _nav = NavRepeat{};
    _navScreen     = active;
    const i32 step = _nav.Step(input.JustPressed(ActionFocusNext), input.JustPressed(ActionFocusPrev),
                               input.IsPressed(ActionFocusNext), input.IsPressed(ActionFocusPrev), dt);

    // Tab always moves focus regardless of whether a text widget is focused.
    if (input.JustPressed(ActionTabNext)) active->FocusNext();
    else if (!prevConsumesNav) {
        if (step > 0) active->FocusNext();
        else if (step < 0) active->FocusPrev();
    }

    // Focus-move sound — read from the newly-focused widget's effective skin so
    // per-widget overrides apply.
    if (UIWidget* const nowFocused = active->GetFocusedWidget(); nowFocused != prevFocused) {
        const UISkin src = nowFocused ? nowFocused->GetEffectiveSkin(_skin) : _skin;
        if (!src.SoundFocusMove.empty()) AudioManager::PlayOneShot(src.SoundFocusMove);
    }

    // Enter activates the focused widget (fires OnClick for buttons, OnConfirm
    // for TextInput) - unless the gamepad cursor is up, when South clicks
    // under the cursor instead.
    if (input.JustPressed(ActionConfirm) && !_cursorActive) active->ActivateFocused();

    // Escape / gamepad East: clear text focus or navigate back.
    // ActionTextEscape = Key::Escape; ActionBack = GamepadButton::East.
    // Both share the same behavior: cancel text editing when focused, else back.
    if (IsBackPressed(input)) {
        if (prevConsumesNav) active->ClearFocus();
        else {
            if (!_skin.SoundBack.empty()) AudioManager::PlayOneShot(_skin.SoundBack);
            active->OnBackPressed();
        }
    }

    // Forward typed characters to the focused widget.
    if (!input.GetTextInput().empty()) active->DispatchTextInput(input.GetTextInput());

    // Dispatch control keys with modifier awareness.
    const bool shift = input.IsPressed(ActionShiftMod);
    const bool ctrl  = input.IsPressed(ActionCtrlMod);

    auto dispatch = [&](UIControlKey plain, UIControlKey    withShift,
                        UIControlKey withCtrl, UIControlKey withCtrlShift) {
        if (ctrl && shift) active->DispatchControlKey(withCtrlShift);
        else if (ctrl) active->DispatchControlKey(withCtrl);
        else if (shift) active->DispatchControlKey(withShift);
        else active->DispatchControlKey(plain);
    };

    if (input.JustPressed(ActionTextLeft))
        dispatch(UIControlKey::Left, UIControlKey::ShiftLeft,
                 UIControlKey::CtrlLeft, UIControlKey::CtrlShiftLeft);
    if (input.JustPressed(ActionTextRight))
        dispatch(UIControlKey::Right, UIControlKey::ShiftRight,
                 UIControlKey::CtrlRight, UIControlKey::CtrlShiftRight);
    if (input.JustPressed(ActionTextHome))
        dispatch(UIControlKey::Home, UIControlKey::ShiftHome,
                 UIControlKey::Home, UIControlKey::ShiftHome);
    if (input.JustPressed(ActionTextEnd))
        dispatch(UIControlKey::End, UIControlKey::ShiftEnd,
                 UIControlKey::End, UIControlKey::ShiftEnd);
    if (input.JustPressed(ActionTextBackspace)) active->DispatchControlKey(UIControlKey::Backspace);
    if (input.JustPressed(ActionTextDelete)) active->DispatchControlKey(UIControlKey::Delete);

    // Sync SDL text input mode against the *final* focus state for the frame.
    // Done last so any focus change above (Tab/arrow nav, ClearFocus on back,
    // ActivateFocused) is reflected before the next SDL pump.
    UIWidget* const finalFocused = active->GetFocusedWidget();
    SyncTextInput(finalFocused && finalFocused->ConsumesFocusNav());
}

void UIManager::SyncTextInput(const bool active)
{
    if (active == _textInputActive) return;
    _textInputActive = active;
    if (_textInputActiveCallback) _textInputActiveCallback(_textInputActive);
}

// ---------------------------------------------------------------------------
// CollectRenderData
// ---------------------------------------------------------------------------

void UIManager::CollectRenderData(FramePacket& packet, const Vec2 windowSize)
{
    if (_stack.empty()) return;
    const UIRect screenRect = {0.0f, 0.0f, windowSize.X, windowSize.Y};

    // Find the lowest-index game-blocking screen (not fading out).
    // Skip screens below it — they'd be entirely hidden by the blocker anyway.
    i32 startIdx = 0;
    for (i32 i = 0; i < static_cast<i32>(_stack.size()); ++i) {
        if (_stack[i]->BlocksGame && !_stack[i]->IsFadingOut()) {
            startIdx = i;
            break;
        }
    }

    for (i32 i = startIdx; i < static_cast<i32>(_stack.size()); ++i) {
        UISkin screenSkin          = _skin;
        screenSkin.ScreenLayerBase = i * 1000;
        _stack[i]->Collect(packet, screenRect, screenSkin, _whiteTex, _whiteSampler);
    }
    CollectCursor(packet);
}

// A small diamond - light, with a dark rim so it reads on any background.
void UIManager::CollectCursor(FramePacket& packet) const
{
    if (!_cursorActive) return;
    constexpr f32 Diamond = 0.785398f; // 45 degrees
    const i32     top     = std::numeric_limits<i32>::max() - 1;
    for (const auto& [size, tint, layer] : { std::tuple{ 18.0f, Color{ 0.05f, 0.05f, 0.08f, 0.9f }, top - 1 },
                                             std::tuple{ 12.0f, Color{ 1.0f, 0.95f, 0.75f, 1.0f }, top } }) {
        Sprite s{};
        s.Texture  = _whiteTex;
        s.Sampler  = _whiteSampler;
        s.Position = _cursorPos;
        s.Size     = { size, size };
        s.Tint     = tint;
        s.Rotation = Diamond;
        s.Layer    = layer;
        packet.UISprites.push_back(s);
    }
}
} // namespace Arcbit
