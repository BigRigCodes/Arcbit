// The console from Lua: Console.Register adds a command that runs a script
// function (ended with the script that registered it), Console.Print writes
// to it.

#include "ScriptState.h"

#include <arcbit/app/DebugConsole.h>

#include <format>

namespace Arcbit
{
void ScriptEngine::State::EndConsoleCommands(const u64 owner)
{
    if (owner == 0 || !Services.Console) return;
    for (auto it = ConsoleOwners.begin(); it != ConsoleOwners.end();) {
        if (it->second != owner) {
            ++it;
            continue;
        }
        Services.Console->Unregister(it->first);
        it = ConsoleOwners.erase(it);
    }
}

// Console.Register(name, fn, help, cheat): fn gets the words after the
// command's name, as text; an error in it is printed in the console.
void ScriptEngine::State::BindConsole()
{
    sol::table console = Lua.create_table();
    console.set_function("Register", [this](const std::string& name, const sol::protected_function fn,
                                            const sol::optional<std::string> help, const sol::optional<bool> cheat) {
        if (!Services.Console) throw std::runtime_error("Console.Register: no console");
        const ScriptContext context = Current;
        Services.Console->Register(
            { .Name  = name,
              .Help  = help.value_or(name),
              .Cheat = cheat.value_or(false),
              .Run   = [this, context, fn](const DebugConsole::Args& args) {
                  const ContextScope                   scope(*this, context);
                  const sol::protected_function_result result = Call(fn, sol::as_args(args));
                  if (!result.valid()) throw std::runtime_error(result.get<sol::error>().what());
              } });
        ConsoleOwners[name] = context.Owner;
    });
    console.set_function("Print", [this](const sol::variadic_args args) {
        std::string text;
        for (const sol::object arg : args) text += (text.empty() ? "" : " ") + ToString(arg);
        if (Services.Console) Services.Console->Print(text);
        else Print(text);
    });
    AddGlobal("Console", console);
}
} // namespace Arcbit
