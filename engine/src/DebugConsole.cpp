#include <arcbit/app/DebugConsole.h>

#include <arcbit/core/Clipboard.h>
#include <arcbit/core/Log.h>

#include <charconv>
#include <format>
#include <ranges>
#include <stdexcept>

namespace Arcbit
{
DebugConsole::DebugConsole()
{
    Register({ .Name = "help", .Help = "help [command] - list commands, or explain one",
               .Run  = [this](const Args& args) { Help(args); } });
    Register({ .Name = "clear", .Help = "clear - empty the console", .Run = [this](const Args&) { Clear(); } });
    Register({ .Name = "copy", .Help = "copy [lines] - the console's output (the last lines, or all) to the clipboard",
               .Run  = [this](const Args& args) { CopyOutput(args); } });
}

// Everything above this command's own line, or its last n lines.
void DebugConsole::CopyOutput(const Args& args)
{
    const size_t shown = _lines.empty() ? 0 : _lines.size() - 1; // not the "> copy" just echoed
    const size_t count = args.empty() ? shown : std::min<size_t>(Integer(args[0], "lines", 0), shown);
    std::string  text;
    for (size_t i = shown - count; i < shown; ++i) text += _lines[i] + "\n";
    Clipboard::SetText(text);
    Print(std::format("copied {} line{}", count, count == 1 ? "" : "s"));
}

i64 DebugConsole::Integer(const std::string_view word, const std::string_view what, const i64 min, const i64 max)
{
    i64        value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error == std::errc::result_out_of_range) throw std::runtime_error(std::format("{} '{}' is too big", what, word));
    if (error != std::errc{} || end != word.data() + word.size())
        throw std::runtime_error(std::format("{} must be a whole number, not '{}'", what, word));
    if (value < min) throw std::runtime_error(std::format("{} must be at least {}, not {}", what, min, value));
    if (value > max) throw std::runtime_error(std::format("{} must be at most {}, not {}", what, max, value));
    return value;
}

f32 DebugConsole::Decimal(const std::string_view word, const std::string_view what)
{
    f32        value = 0.0f;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size())
        throw std::runtime_error(std::format("{} must be a number, not '{}'", what, word));
    return value;
}

void DebugConsole::Register(Command command)
{
    std::string name = command.Name;
    _commands.insert_or_assign(std::move(name), std::move(command));
}

void DebugConsole::Unregister(const std::string_view name)
{
    if (const auto it = _commands.find(name); it != _commands.end()) _commands.erase(it);
}

const DebugConsole::Command* DebugConsole::Find(const std::string_view name) const
{
    const auto it = _commands.find(name);
    return it != _commands.end() ? &it->second : nullptr;
}

void DebugConsole::Execute(const std::string_view line)
{
    const Args words = Split(line);
    if (words.empty()) return;
    Print(std::format("> {}", line));
    if (_history.empty() || _history.back() != line) _history.emplace_back(line);

    if (const Command* command = Find(words.front())) {
        const Command run = *command; // a command may unregister itself
        if (run.Cheat && OnCheat) OnCheat(run.Name);
        try {
            run.Run(Args(words.begin() + 1, words.end()));
        }
        catch (const std::exception& e) {
            Print(std::format("{}: {}", run.Name, e.what()));
        }
        return;
    }
    if (!RunLua) {
        Print(std::format("unknown command '{}' - try help", words.front()));
        return;
    }
    if (OnCheat) OnCheat("lua");
    RunLua(line);
}

void DebugConsole::Print(const std::string_view text)
{
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        _lines.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    while (_lines.size() > MaxLines) _lines.pop_front();
    ++_version;
}

void DebugConsole::Clear()
{
    _lines.clear();
    ++_version;
}

std::vector<std::string> DebugConsole::Complete(const std::string_view prefix) const
{
    std::vector<std::string> names;
    for (const std::string& name : _commands | std::views::keys)
        if (name.starts_with(prefix)) names.push_back(name);
    return names;
}

std::string DebugConsole::Suggest(const std::string_view line) const
{
    if (line.empty()) return {};
    if (line.find(' ') == std::string_view::npos)
        for (const std::string& name : _commands | std::views::keys)
            if (name.size() > line.size() && name.starts_with(line)) return name.substr(line.size());
    if (CompleteLua) {
        const std::vector<std::string> rest = CompleteLua(line);
        if (!rest.empty()) return rest.front();
    }
    return {};
}

DebugConsole::Args DebugConsole::Split(const std::string_view line)
{
    Args        words;
    std::string word;
    bool        quoted = false, any = false;
    for (const char c : line) {
        if (c == '"') {
            quoted = !quoted;
            any    = true;
        }
        else if (!quoted && (c == ' ' || c == '\t')) {
            if (any) words.push_back(std::move(word));
            word.clear();
            any = false;
        }
        else {
            word += c;
            any = true;
        }
    }
    if (any) words.push_back(std::move(word));
    return words;
}

void DebugConsole::Help(const Args& args)
{
    if (!args.empty()) {
        const Command* command = Find(args.front());
        Print(command ? command->Help : std::format("no command '{}'", args.front()));
        return;
    }
    for (const Command& command : _commands | std::views::values)
        Print(command.Cheat ? command.Help + "  (cheat)" : command.Help);
    if (RunLua) Print("anything else runs as Lua");
}
} // namespace Arcbit
