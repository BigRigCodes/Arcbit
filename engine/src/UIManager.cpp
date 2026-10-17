#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/UILoader.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>

#include <algorithm>
#include <iterator>

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

    // Below the StaysOnTop screens (they sit at the top of the stack).
    auto at = _stack.end();
    if (!screen->StaysOnTop)
        while (at != _stack.begin() && (*std::prev(at))->StaysOnTop) --at;
    _stack.insert(at, std::move(screen));
}

void UIManager::Pop()
{
    // The topmost screen that isn't StaysOnTop or already on its way out.
    auto it = _stack.end();
    while (it != _stack.begin()) {
        const UIScreen* s = std::prev(it)->get();
        if (!s->StaysOnTop && std::ranges::find(_deferredRemovals, s) == _deferredRemovals.end()) break;
        --it;
    }
    if (it == _stack.begin()) return;
    Close(std::prev(it)->get());
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
// Update
// ---------------------------------------------------------------------------

UIScreen* UIManager::ActiveInputScreen() const
{
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
        if (!(*it)->IsFadingOut()) return it->get();
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

void UIManager::UpdateScreens(const f32 dt, const Vec2 windowSize, const InputManager& input)
{
    i32 mx = 0, my = 0;
    input.GetMousePosition(mx, my);
    _mousePos      = {static_cast<f32>(mx), static_cast<f32>(my)};
    _mouseDown     = input.IsMouseButtonDown(MouseButton::Left);
    _mouseJustDown = input.IsMouseButtonJustDown(MouseButton::Left);
    _mouseJustUp   = input.IsMouseButtonJustUp(MouseButton::Left);

    for (const auto& s : _stack) s->AdvanceTransition(dt);
    RemoveCompletedFadeOuts();

    UIScreen* active = ActiveInputScreen();
    if (!active) return;

    // Screens set this flag on the frame they are pushed so that the key event
    // that caused the push (e.g. Escape to open a menu) is not also processed
    // as a back-press on the newly-visible screen. OnTick is skipped too: it
    // reads input directly, so a screen toggled by the same key (F1 opens, F1
    // closes) would otherwise close itself the tick it opened.
    const bool skipInput        = active->_skipInputThisFrame;
    active->_skipInputThisFrame = false;

    const UIRect screenRect = {0.0f, 0.0f, windowSize.X, windowSize.Y};
    if (!skipInput) active->OnTick(dt, input);
    active->Update(dt, screenRect, _mousePos, _mouseDown, _mouseJustDown, _mouseJustUp,
                   input.GetScrollDelta());

    if (skipInput) return;

    // Snapshot focus state before any navigation/back input runs. Used to:
    //  - gate arrow-key navigation (text-consuming widgets swallow arrows)
    //  - decide whether back-press cancels editing or pops the screen
    //  - detect focus changes for the focus-move sound below
    UIWidget* const prevFocused      = active->GetFocusedWidget();
    const bool      prevConsumesNav  = prevFocused && prevFocused->ConsumesFocusNav();

    // Tab always moves focus regardless of whether a text widget is focused.
    if (input.JustPressed(ActionTabNext)) active->FocusNext();
    else if (!prevConsumesNav) {
        if (input.JustPressed(ActionFocusNext)) active->FocusNext();
        if (input.JustPressed(ActionFocusPrev)) active->FocusPrev();
    }

    // Focus-move sound — read from the newly-focused widget's effective skin so
    // per-widget overrides apply.
    if (UIWidget* const nowFocused = active->GetFocusedWidget(); nowFocused != prevFocused) {
        const UISkin src = nowFocused ? nowFocused->GetEffectiveSkin(_skin) : _skin;
        if (!src.SoundFocusMove.empty()) AudioManager::PlayOneShot(src.SoundFocusMove);
    }

    // Enter activates the focused widget (fires OnClick for buttons, OnConfirm for TextInput).
    if (input.JustPressed(ActionConfirm)) active->ActivateFocused();

    // Escape / gamepad East: clear text focus or navigate back.
    // ActionTextEscape = Key::Escape; ActionBack = GamepadButton::East.
    // Both share the same behavior: cancel text editing when focused, else back.
    const bool backPressed = input.JustPressed(ActionTextEscape) ||
            input.JustPressed(ActionBack);
    if (backPressed) {
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
    const bool      consumesNav  = finalFocused && finalFocused->ConsumesFocusNav();
    if (consumesNav != _textInputActive) {
        _textInputActive = consumesNav;
        if (_textInputActiveCallback) _textInputActiveCallback(_textInputActive);
    }
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
}
} // namespace Arcbit
