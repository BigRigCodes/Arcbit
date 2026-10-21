#pragma once

#include <arcbit/core/Loc.h>
#include <arcbit/dialog/Dialog.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class EventBus;
class SaveData;

// What a running dialog reads and changes: the game's SaveData (conditions,
// set / add, {placeholders}), the event bus ("event"), and Lua ("lua" in an
// "if" is Test, as an action Run; "call" is Call). Any can be left unset:
// without Data every key reads as missing; a "lua" condition without Test
// fails and an action without its hook is skipped (both logged).
struct DialogHost
{
    SaveData*                               Data   = nullptr;
    EventBus*                               Events = nullptr;
    std::function<bool(std::string_view)>   Test;
    std::function<void(std::string_view)>   Run;
    std::function<void(std::string_view)>   Call;
    std::vector<LocArg>                     Args; // more placeholders, ahead of SaveData's: {player}
};

// ---------------------------------------------------------------------------
// DialogRunner - walks a Dialog: shows a line, waits for Next; offers a
// choice, waits for Choose; runs everything else (branches, actions, gotos)
// on the way. No drawing or input - the dialog box (DialogScreen) drives it.
//
//   runner.Start(library.Get("elder"), host);
//   while (runner.IsRunning()) {
//       if (runner.GetState() == DialogRunner::State::Line) Show(runner.GetLine()), runner.Next();
//       else runner.Choose(PickOne(runner.GetChoices()));
//   }
//
// Text is looked up in the active language as each line comes, its
// {placeholders} filled from the host's Args, then SaveData - a key that
// isn't saved yet reads 0, as in a condition.
// ---------------------------------------------------------------------------
class DialogRunner
{
public:
    // Steps run without a line or a choice before the walk gives up - a
    // loop of gotos (logged, and the dialog ends).
    static constexpr u32 MaxSteps = 1000;

    enum class State : u8 { Idle, Line, Choice, Ended };

    struct Line
    {
        const DialogSpeaker* Speaker = nullptr; // null: narration
        std::string          Name;              // the speaker's, localized
        std::string          Text;              // localized, placeholders filled; markup left in
        std::string          Key;
        std::string          Portrait;          // the line's, else the speaker's
        std::string          Voice;
    };

    struct Choice
    {
        std::string Text;
        std::string Id;
        u32         Option = 0; // in the step's options
    };

    // Told as each line shows and as the player chooses (the choice's id) -
    // Application publishes them as engine events.
    std::function<void(const Line&)>               OnLine;
    std::function<void(const std::string& choice)> OnChoose;

    // Whether backing out of a line ends a dialog that doesn't say
    // ("cancelable") - Application sets the project default.
    bool CancelableByDefault = true;

    // From node (default: the dialog's start). A null dialog ends at once.
    void Start(std::shared_ptr<const Dialog> dialog, DialogHost host, std::string_view node = {});
    void Next();             // past the line shown
    void Choose(u32 choice); // an index in GetChoices()
    void Stop();             // end now; GetLastChoice reads ""

    // Backing out (Esc): on a choice, picks the option marked "cancel" (the
    // first shown; none, nothing happens); on a line, ends the dialog with
    // no last choice - if it's cancelable. CanCancel: whether Cancel would.
    void                             Cancel();
    [[nodiscard]] bool               CanCancel() const;
    [[nodiscard]] std::optional<u32> CancelChoice() const; // an index in GetChoices()

    [[nodiscard]] State GetState() const { return _state; }
    [[nodiscard]] bool  IsRunning() const { return _state == State::Line || _state == State::Choice; }

    // The line shown; during a choice, the line before it (still on screen).
    [[nodiscard]] const Line&                GetLine() const { return _line; }
    [[nodiscard]] const std::vector<Choice>& GetChoices() const { return _choices; }
    [[nodiscard]] const std::string&         GetLastChoice() const { return _lastChoice; } // its "id"
    [[nodiscard]] const Dialog*              GetDialog() const { return _dialog.get(); }

    // Look the line and choices up again (the language changed).
    void RefreshText();

    // Whether a condition holds against a host (also for tests and tools).
    [[nodiscard]] static bool Holds(const DialogCondition& condition, const DialogHost& host);

private:
    void Walk();
    void Follow(const std::string& go);
    void Act(const DialogActions& actions);
    void ShowLine(const DialogStep& step);
    void FillLine(const DialogStep& step);
    bool OfferChoice(const DialogStep& step);
    void End();

    [[nodiscard]] std::string Text(const std::string& key) const;

    std::shared_ptr<const Dialog> _dialog;
    DialogHost                    _host;
    const DialogNode*             _node  = nullptr;
    u32                           _step  = 0;
    const DialogStep*             _lineStep = nullptr; // the line shown
    State                         _state = State::Idle;
    Line                          _line;
    std::vector<Choice>           _choices;
    std::string                   _lastChoice;
};
} // namespace Arcbit
