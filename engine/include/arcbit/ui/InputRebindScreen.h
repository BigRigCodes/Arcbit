#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/input/InputTypes.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class InputManager;
class Button;
class ScrollPanel;

// ---------------------------------------------------------------------------
// InputRebindScreen — chip-based input binding editor.
//
// Each action displays its bindings as removable chips in two columns:
// Key/Mouse and Controller.  Clicking a chip replaces that binding; clicking
// "+" adds a new one.  Input is captured on RELEASE to avoid recording the
// click that opened listening mode, which means LMB is bindable normally.
//
// Requires Input to be set before Push.  Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class InputRebindScreen : public UIScreen
{
public:
    InputManager* Input = nullptr;

    std::function<void()> OnBack;

    // Action names starting with any of these prefixes are hidden.
    std::vector<std::string> HiddenPrefixes = {"UI_", "Engine_", "Console_"};

    std::string LayoutPath = "assets/engine/ui/input_rebind.arcui";

    InputRebindScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
    void OnExit() override;
    void OnBackPressed() override;
    void OnTick(f32 dt, const InputManager& input) override;

private:
    struct ActionEntry { ActionID id; std::string display, category; };

    std::vector<ActionEntry> _entries;  // sorted visible actions; stable across rebuilds

    // Listening state
    bool        _isListening      = false;
    ActionID    _listeningAction  = 0;
    bool        _listeningForKey  = true;   // true = Key/Mouse column, false = Controller
    Button*     _listeningButton  = nullptr; // button being flashed
    f32         _flashTimer       = 0.0f;
    bool        _hadRemoved       = false;   // whether we removed a binding to replace it
    Binding     _removedBinding   = {};      // restored on cancel

    ScrollPanel* _scroll         = nullptr;
    bool         _pendingRebuild = false; // deferred RebuildScroll to avoid re-entering UpdateTree

    void BuildEntries();
    void RebuildScroll();
    void AddChipColumn(ActionID action, bool forKey,
                       f32 colX, f32 startY, f32 chipH, f32 chipGap);
    [[nodiscard]] i32 CountBindings(ActionID action, bool forKey) const;
    void StartListening(ActionID action, bool forKey, Button* btn,
                        bool replaceExisting, Binding toReplace = {});
    void CancelListening();  // restores removed binding, does NOT rebuild
    void StopListening(bool commit);
    void RemoveChip(ActionID action, const Binding& b);
    void TakeBinding(const Binding& b); // from every action listed here, then to the listening one
};

} // namespace Arcbit
