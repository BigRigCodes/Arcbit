// Tasks: coroutines a script starts (self:Run, Task.Run, Timer) that pause
// with Wait and pick up again on a later tick - a patrol, a door closing
// after a delay, a short scripted beat. Each belongs to the script that
// started it and its scene; it advances with the scene's update and ends with
// its script.

#include "ScriptState.h"

#include <format>

#include <arcbit/core/Log.h>
#include <arcbit/scene/Scene.h>

#include <vector>

namespace Arcbit
{
namespace
{
    // Wait / WaitUntil and the timers, in Lua: they only yield, and the
    // scheduler below reads how long from the yielded number.
    constexpr const char* Prelude = R"lua(
local Task = ...
local yield, isyieldable = coroutine.yield, coroutine.isyieldable

local function Wait(seconds)
    if not isyieldable() then error("Wait only works inside a task (self:Run, Task.Run, Timer)", 2) end
    yield(seconds or 0)
end

local function WaitUntil(test)
    while not test() do Wait(0) end
end

local Timer = {}
function Timer.After(seconds, fn) return Task.Run(function() Wait(seconds) fn() end) end
function Timer.Every(seconds, fn) return Task.Run(function() while true do Wait(seconds) fn() end end) end

local function SelfRun(_, fn, ...) return Task.Run(fn, ...) end

return Wait, WaitUntil, Timer, SelfRun
)lua";
} // namespace

// Resume a task until it yields (true: it waits again) or ends (false: done,
// or failed - logged with its traceback). The instruction budget applies.
bool ScriptEngine::State::ResumeTask(ScriptTask& task, lua_State* from)
{
    lua_State* T       = task.Thread.thread_state();
    const int  args    = task.Started ? 0 : task.Arguments;
    int        results = 0;
    task.Started       = true;
    task.Running       = true;
    int status         = 0;
    {
        const CallScope scope(Runtime);
        status = lua_resume(T, from, args, &results);
    }
    task.Running = false;
    if (status == LUA_YIELD) {
        task.Wait = results > 0 && lua_isnumber(T, -1) ? lua_tonumber(T, -1) : 0.0;
        lua_pop(T, results);
        return !task.Stopped;
    }
    if (status != LUA_OK) {
        luaL_traceback(from, T, lua_tostring(T, -1), 0);
        ReportError(std::format("Task: {}", lua_tostring(from, -1)));
        lua_pop(from, 1);
    }
    return false;
}

// The task runs at once, up to its first Wait.
u64 ScriptEngine::State::StartTask(lua_State* L, const sol::protected_function& fn, const sol::variadic_args& args)
{
    Scene* scene = ContextScene();
    if (!scene || SerialOf(scene) == 0) throw std::runtime_error("Task.Run: no scene running scripts to run it in");

    const u64  id   = NextTaskId++;
    ScriptTask task;
    task.Thread    = sol::thread::create(L);
    task.Context   = Current; // runs as its starter; a UI script's or the console's, in the active scene
    if (!task.Context.Scene) {
        task.Context.Scene  = scene;
        task.Context.Serial = SerialOf(scene);
    }
    lua_State* T   = task.Thread.thread_state();
    fn.push(T);
    for (const sol::object arg : args) arg.push(T);
    task.Arguments = static_cast<int>(args.size());

    ScriptTask& running = Tasks[id] = std::move(task);
    if (!ResumeTask(running, L)) Tasks.erase(id);
    return id;
}

// Every task of the scene whose wait is over, resumed as its owner.
void ScriptEngine::State::AdvanceTasks(const u64 serial, const f32 dt)
{
    std::vector<u64> due;
    for (auto& [id, task] : Tasks) {
        if (task.Context.Serial != serial || task.Stopped) continue;
        task.Wait -= dt;
        if (task.Wait <= 0.0) due.push_back(id);
    }
    for (const u64 id : due) {
        const auto it = Tasks.find(id);
        if (it == Tasks.end() || it->second.Stopped) continue;
        const ContextScope scope(*this, it->second.Context);
        if (!ResumeTask(it->second, Lua.lua_state())) it->second.Stopped = true;
    }
    std::erase_if(Tasks, [](const auto& entry) { return entry.second.Stopped && !entry.second.Running; });
}

// Stop tasks by owner or by scene. One that's running right now (it stopped
// itself, or destroyed its own entity) is only marked: its coroutine can't be
// released mid-run, so it goes once it yields.
void ScriptEngine::State::StopTasks(const u64 owner, const u64 serial)
{
    for (auto it = Tasks.begin(); it != Tasks.end();) {
        const bool match = (owner != 0 && it->second.Context.Owner == owner) || (serial != 0 && it->second.Context.Serial == serial);
        if (!match) {
            ++it;
            continue;
        }
        it->second.Stopped = true;
        it                 = it->second.Running ? std::next(it) : Tasks.erase(it);
    }
}

// Task.Run(fn, ...), task:Stop() / IsRunning(), and the Lua-side Wait,
// WaitUntil, Timer and self:Run.
void ScriptEngine::State::BindTasks()
{
    sol::usertype<ScriptTaskHandle> type = Lua.new_usertype<ScriptTaskHandle>(
        "Task", sol::no_constructor,
        "Stop", [this](const ScriptTaskHandle& h) {
            const auto it = Tasks.find(h.Id);
            if (it == Tasks.end()) return;
            it->second.Stopped = true;
            if (!it->second.Running) Tasks.erase(it);
        },
        "IsRunning", [this](const ScriptTaskHandle& h) {
            const auto it = Tasks.find(h.Id);
            return it != Tasks.end() && !it->second.Stopped;
        });
    type["Run"] = [this](const sol::this_state L, const sol::protected_function fn, const sol::variadic_args args) {
        return ScriptTaskHandle{ StartTask(L, fn, args) };
    };
    AddGlobal("Task", Lua["Task"]);

    sol::load_result prelude = Lua.load(Prelude, "=tasks", sol::load_mode::text);
    sol::protected_function_result helpers = prelude.get<sol::protected_function>()(Lua["Task"]);
    if (!helpers.valid()) throw std::runtime_error(helpers.get<sol::error>().what());
    AddGlobal("Wait", helpers.get<sol::object>(0));
    AddGlobal("WaitUntil", helpers.get<sol::object>(1));
    AddGlobal("Timer", helpers.get<sol::object>(2));
    SelfRun = helpers.get<sol::object>(3);
}
} // namespace Arcbit
