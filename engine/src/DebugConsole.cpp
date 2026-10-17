#include <arcbit/app/DebugConsole.h>

#include <arcbit/core/Log.h>

#include <format>
#include <ranges>

namespace Arcbit
{
DebugConsole::DebugConsole()
{
    Register({ .Name = "help", .Help = "help [command] - list commands, or explain one",
               .Run  = [this](const Args& args) { Help(args); } });
    Register({ .Name = "clear", .Help = "clear - empty the console", .Run = [this](const Args&) { Clear(); } });
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
