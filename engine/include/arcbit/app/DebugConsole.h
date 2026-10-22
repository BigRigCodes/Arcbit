#pragma once

#include <arcbit/core/Types.h>

#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// DebugConsole - a Skyrim-style console (docs/scripting.md): named commands
// with help text, registered from C++ or Lua, and Lua for anything that isn't
// a command. This is the logic; ConsoleScreen shows it, and Application
// decides who may open it (ConsoleAccess).
//
//   console.Register({ .Name = "heal", .Help = "heal <amount> - restore health",
//                      .Cheat = true, .Run = [](const std::vector<std::string>& args) { ... } });
//   console.Execute("heal 50");
//
// A command marked Cheat (and any Lua typed in) calls OnCheat when it runs -
// Application records that in the save data (engine.cheats_used).
// ---------------------------------------------------------------------------
class DebugConsole
{
public:
    using Args = std::vector<std::string>;

    struct Command
    {
        std::string                      Name;
        std::string                      Help; // "tp <x> <y> - move the player"
        bool                             Cheat = false;
        std::function<void(const Args&)> Run; // throw std::exception to report a mistake
    };

    DebugConsole(); // registers help and clear

    // Add a command (replacing one with the same name) / take one away.
    void Register(Command command);
    void Unregister(std::string_view name);
    [[nodiscard]] const Command* Find(std::string_view name) const;

    // Run a line: echoed, kept in the history, then a command by its first
    // word, or else Lua (RunLua). Errors are printed, never thrown.
    void Execute(std::string_view line);

    // Output: each line of `text` (split on newlines), oldest dropped past MaxLines.
    void Print(std::string_view text);
    void Clear();
    [[nodiscard]] const std::deque<std::string>& GetLines() const { return _lines; }
    [[nodiscard]] u64                            GetVersion() const { return _version; } // changes with the lines

    [[nodiscard]] const std::vector<std::string>& GetHistory() const { return _history; }

    // Command names starting with `prefix`, sorted - for Tab.
    [[nodiscard]] std::vector<std::string> Complete(std::string_view prefix) const;

    // The likeliest rest of the line as typed so far ("lp" for "he"): a
    // command name while the first word is being typed, else whatever
    // CompleteLua offers for the name at the end. Empty if nothing fits.
    [[nodiscard]] std::string Suggest(std::string_view line) const;

    // Words, with "quoted text" kept as one.
    [[nodiscard]] static Args Split(std::string_view line);

    // A command's argument as a number. The whole word must be one, within
    // [min, max]; otherwise they throw a message naming it by `what`
    // ("count must be a whole number, not 'a'"), which Execute prints.
    [[nodiscard]] static i64 Integer(std::string_view word, std::string_view what,
                                     i64 min = std::numeric_limits<i64>::min(), i64 max = std::numeric_limits<i64>::max());
    [[nodiscard]] static f32 Decimal(std::string_view word, std::string_view what);

    std::function<void(std::string_view code)> RunLua;  // what isn't a command; null = "unknown command"
    std::function<std::vector<std::string>(std::string_view line)> CompleteLua; // remainders, sorted (ScriptEngine::Complete)
    std::function<void(std::string_view what)> OnCheat; // a cheat command (or Lua) ran
    size_t                                     MaxLines = 400;

private:
    void CopyOutput(const Args& args);

    std::map<std::string, Command, std::less<>> _commands; // sorted, for help and completion
    std::deque<std::string>                     _lines;
    std::vector<std::string>                    _history;
    u64                                         _version = 0;

    void Help(const Args& args);
};
} // namespace Arcbit
