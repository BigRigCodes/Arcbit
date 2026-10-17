#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/ui/UIScreen.h>

#include <deque>
#include <functional>
#include <string>

namespace Arcbit
{
class DebugConsole;
class ScrollPanel;
class TextInput;

// ---------------------------------------------------------------------------
// ConsoleScreen - the debug console's window: the output over the top of the
// screen and a line to type in. Enter runs the line, Up / Down walk the
// history, Tab completes a command name, Back closes it. Application opens
// and closes it on its key (ConsoleAccess decides who may).
// ---------------------------------------------------------------------------
class ConsoleScreen : public UIScreen
{
public:
    static constexpr ActionID ActionHistoryUp   = MakeAction("Console_HistoryUp");
    static constexpr ActionID ActionHistoryDown = MakeAction("Console_HistoryDown");
    static constexpr ActionID ActionComplete    = MakeAction("Console_Complete");

    DebugConsole*         Console = nullptr;
    std::function<void()> OnClose;

    ConsoleScreen()
    {
        Layer           = UILayer::Overlay; // over menus too
        BlocksInput     = true;
        TransitionSpeed = 0.0f;
    }

    void OnEnter() override;
    void OnTick(f32 dt, const InputManager& input) override;
    void OnUpdate(f32 dt) override;
    void OnBackPressed() override { if (OnClose) OnClose(); }

private:
    ScrollPanel* _scroll        = nullptr;
    TextInput*   _input         = nullptr;
    u64          _shownVersion  = ~0ull;
    i32          _historyCursor = -1; // -1 = editing a new line

    void ShowLines();
    void Submit(const std::string& line);
    void Recall(i32 step);
    void CompleteCommand();
};

// ---------------------------------------------------------------------------
// ErrorToasts - script errors in the top-right corner for a few seconds
// (debug builds), so a broken script is noticed without watching the log.
// ---------------------------------------------------------------------------
class ErrorToasts : public UIScreen
{
public:
    f32    Seconds = 8.0f; // each stays, its last second fading
    size_t MaxShown = 4;

    ErrorToasts()
    {
        Layer           = UILayer::Overlay;
        TakesInput      = false;
        TransitionSpeed = 0.0f;
    }

    // Shows the first line of `error`.
    void Report(std::string_view error);

    void OnUpdate(f32 dt) override;

private:
    struct Toast
    {
        std::string Text;
        f32         Age = 0.0f;
    };
    std::deque<Toast> _toasts;
    bool              _changed = false;

    void Rebuild();
};
} // namespace Arcbit
