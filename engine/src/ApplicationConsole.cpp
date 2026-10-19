// The debug console in the running game: who may open it, its engine
// commands, and Lua / script errors flowing into it. Split out of
// Application.cpp, which owns the main loop.

#include <arcbit/app/Application.h>
#include <arcbit/core/Loc.h>

#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/ConsoleScreen.h>

#include <charconv>
#include <format>

namespace Arcbit
{
namespace
{
    constexpr const char* CheatsUsedKey = "engine.cheats_used"; // SaveData: a cheat ran in this game

    f32 ToNumber(const std::string& text)
    {
        f32 value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            throw std::runtime_error(std::format("'{}' isn't a number", text));
        return value;
    }

    // "quick", or a manual slot number.
    SaveSlot ToSlot(const DebugConsole::Args& args)
    {
        if (args.empty() || args[0] == "quick") return SaveSlot::Quick();
        return SaveSlot::Manual(static_cast<u32>(ToNumber(args[0])));
    }

    // A value typed for SaveData: true / false, an integer, a decimal, else text.
    SaveValue ToSaveValue(const std::string& text)
    {
        if (text == "true" || text == "false") return text == "true";
        i64 integer = 0;
        if (const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), integer);
            error == std::errc{} && end == text.data() + text.size())
            return integer;
        f64 decimal = 0.0;
        if (const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), decimal);
            error == std::errc{} && end == text.data() + text.size())
            return decimal;
        return text;
    }

    // The player: the first Interactor, else the first entity tagged Player.
    Entity FindPlayer(World& world)
    {
        Entity player = Entity::Invalid();
        world.Query<Interactor>().ForEach([&](const Entity e, const Interactor&) {
            if (!player.IsValid()) player = e;
        });
        if (!player.IsValid())
            world.Query<Tag>().ForEach([&](const Entity e, const Tag& t) {
                if (!player.IsValid() && t.Value == "Player") player = e;
            });
        return player;
    }
} // namespace

bool Application::IsConsoleAllowed() const
{
    switch (_consoleConfig.Access) {
        case ConsoleAccess::Players: return true;
#ifdef ARCBIT_DEBUG
        case ConsoleAccess::DevOnly: return true;
#endif
        default: return false;
    }
}

// Lua runs what isn't a command; print and script errors land in the console
// (and, in debug builds, errors on screen too).
void Application::StartConsole()
{
    _debugConsole.RunLua = [this](const std::string_view code) {
        const ScriptResult result = _scriptEngine.Run(code);
        if (!result.Ok) return; // the error handler printed it
        std::string values;
        for (const std::string& value : result.Values) values += (values.empty() ? "" : "\t") + value;
        if (!values.empty()) _debugConsole.Print(values);
    };
    _debugConsole.OnCheat     = [this](std::string_view) { _saves.Data().Set(CheatsUsedKey, true); };
    _debugConsole.CompleteLua = [this](const std::string_view line) { return _scriptEngine.Complete(line); };
    _scriptEngine.SetOutput([this](const std::string_view line) { _debugConsole.Print(line); });
    _scriptEngine.SetErrorHandler([this](const std::string_view error) {
        _debugConsole.Print(error);
        if (_errorToasts) _errorToasts->Report(error);
    });
#ifdef ARCBIT_DEBUG
    auto toasts   = std::make_unique<ErrorToasts>();
    _errorToasts  = toasts.get();
    _ui.Push(std::move(toasts));
#endif
    RegisterConsoleCommands();
}

void Application::ToggleConsole()
{
    if (_consoleScreen && _ui.IsOpen(_consoleScreen)) {
        _ui.Remove(_consoleScreen);
        _consoleScreen = nullptr;
        return;
    }
    auto screen       = std::make_unique<ConsoleScreen>();
    screen->Console   = &_debugConsole;
    screen->FontScale = _consoleConfig.FontScale;
    screen->Height    = _consoleConfig.Height;
    screen->OnClose = [this] { ToggleConsole(); };
    _consoleScreen  = screen.get();
    _ui.Push(std::move(screen));
}

// The keys a language has no string for, a screenful at most.
void Application::PrintMissing(const std::string_view code)
{
    constexpr usize                Shown   = 40;
    const std::vector<std::string> missing = Loc::GetMissing(code);
    const std::string_view         name    = code.empty() ? std::string_view(Loc::GetLanguage()) : code;
    _debugConsole.Print(std::format("{}: {} key(s) missing", name, missing.size()));
    for (usize i = 0; i < std::min(missing.size(), Shown); ++i) _debugConsole.Print("  " + missing[i]);
    if (missing.size() > Shown) _debugConsole.Print(std::format("  ... and {} more", missing.size() - Shown));
}

void Application::RegisterConsoleCommands()
{
    using Args = DebugConsole::Args;
    const auto scene = [this]() -> Scene& {
        Scene* active = _scenes.GetActive();
        if (!active) throw std::runtime_error("no scene is running");
        return *active;
    };
    _debugConsole.Register({ .Name = "scene", .Help = "scene <name> [entry] - switch scenes", .Run = [this](const Args& a) {
        if (a.empty() || !_scenes.IsRegistered(a[0])) throw std::runtime_error("name a registered scene");
        _scenes.Switch(a[0], a.size() > 1 ? a[1] : "");
    } });
    _debugConsole.Register({ .Name = "tp", .Help = "tp <x> <y> - move the player", .Cheat = true,
                             .Run = [scene](const Args& a) {
        if (a.size() < 2) throw std::runtime_error("tp <x> <y>");
        Scene&       s      = scene();
        const Entity player = FindPlayer(s.GetWorld());
        if (!player.IsValid()) throw std::runtime_error("no player (an Interactor, or tag Player)");
        const Vec2 to{ ToNumber(a[0]), ToNumber(a[1]) };
        s.GetWorld().GetTransform(player)->Position = to;
        if (auto* tile = s.GetWorld().GetComponent<SmoothTileMovement>(player)) {
            tile->OriginWorld = tile->TargetWorld = to;
            tile->Progress    = 1.0f;
        }
        if (const Collider2D* col = s.GetWorld().GetComponent<Collider2D>(player))
            s.GetPhysics().UpdateCollider(s.GetPhysics().FindColliderForEntity(player), *col, to);
        s.GetCamera().Position = to;
    } });
    _debugConsole.Register({ .Name = "timescale", .Help = "timescale <n> - game speed (1 normal, 0 paused)", .Cheat = true,
                             .Run = [this](const Args& a) {
        _timeScale = a.empty() ? 1.0f : std::clamp(ToNumber(a[0]), 0.0f, 10.0f);
        _debugConsole.Print(std::format("timescale {}", _timeScale));
    } });
    RegisterSaveCommands();
    RegisterDialogCommands();
    _debugConsole.Register({ .Name = "fps", .Help = "fps - show / hide the FPS counter", .Run = [](const Args&) {
        Settings::Graphics.ShowFps = !Settings::Graphics.ShowFps;
        Settings::MarkDirty("graphics");
    } });
    _debugConsole.Register({ .Name = "colliders", .Help = "colliders - show / hide collider outlines", .Run = [scene](const Args&) {
        PhysicsWorld& physics = scene().GetPhysics();
        physics.SetDebugDraw(!physics.GetDebugDraw());
    } });
    _debugConsole.Register({ .Name = "lang", .Help = "lang [code] - list the languages, or switch to one",
                             .Run = [this](const Args& a) {
                                 if (!a.empty()) {
                                     if (!SetLanguage(a[0])) throw std::runtime_error(std::format("no language '{}'", a[0]));
                                     return;
                                 }
                                 for (const Loc::Locale& locale : Loc::GetLocales())
                                     _debugConsole.Print(std::format("{} {} - {}", locale.Code == Loc::GetLanguage() ? '*' : ' ',
                                                                     locale.Code, locale.Name));
                             } });
    _debugConsole.Register({ .Name = "loc.missing", .Help = "loc.missing [code] - English keys a language lacks",
                             .Run = [this](const Args& a) { PrintMissing(a.empty() ? "" : a[0]); } });
    _debugConsole.Register({ .Name = "quit", .Help = "quit - close the game", .Run = [this](const Args&) { RequestShutdown(); } });
    _debugConsole.Register({ .Name = "screenshot", .Help = "screenshot - save the screen (without the console) as a PNG",
                             .Run = [this](const Args&) {
                                 if (_consoleScreen) ToggleConsole(); // out of the picture
                                 Screenshot();
                             } });
}

void Application::RegisterSaveCommands()
{
    using Args = DebugConsole::Args;
    _debugConsole.Register({ .Name = "save", .Help = "save [slot | quick] - save the game", .Run = [this](const Args& a) {
        _debugConsole.Print(SaveTo(ToSlot(a)) ? "saved" : "couldn't save now");
    } });
    _debugConsole.Register({ .Name = "load", .Help = "load [slot | quick] - load a save", .Run = [this](const Args& a) {
        if (!LoadFrom(ToSlot(a))) _debugConsole.Print("couldn't load that");
    } });
    _debugConsole.Register({ .Name = "set", .Help = "set <key> <value> - change save data", .Cheat = true,
                             .Run = [this](const Args& a) {
        if (a.size() < 2) throw std::runtime_error("set <key> <value>");
        _saves.Data().Set(a[0], ToSaveValue(a[1]));
    } });
    _debugConsole.Register({ .Name = "get", .Help = "get <key> - read save data", .Run = [this](const Args& a) {
        if (a.empty()) throw std::runtime_error("get <key>");
        const SaveValue* value = _saves.Data().Find(a[0]);
        _debugConsole.Print(value ? std::visit([](const auto& v) {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, Vec2>) return std::format("({}, {})", v.X, v.Y);
            else return std::format("{}", v);
        }, *value) : "(not set)");
    } });
}
} // namespace Arcbit
