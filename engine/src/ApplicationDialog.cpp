// Application: conversations - starting them, the queue, holding who's in
// one, and the dialog box they show in (docs/dialog.md).

#include <arcbit/app/Application.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Log.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/scene/Scene.h>

#include <cmath>
#include <format>
#include <stdexcept>

namespace Arcbit
{
namespace
{
// The one talked to turns to whoever talks to it.
void TurnToward(World& world, const Entity who, const Entity toward)
{
    MoveIntent*  intent = world.GetComponent<MoveIntent>(who);
    Transform2D* from   = world.GetTransform(who);
    Transform2D* to     = world.GetTransform(toward);
    if (!intent || !from || !to) return;
    const Vec2 d = to->Position - from->Position;
    if (d == Vec2{}) return;
    intent->Facing = std::abs(d.X) >= std::abs(d.Y) ? Vec2{ d.X > 0.0f ? 1.0f : -1.0f, 0.0f }
                                                    : Vec2{ 0.0f, d.Y > 0.0f ? 1.0f : -1.0f };
}
} // namespace

std::unique_ptr<DialogScreen> Application::CreateDialogScreen()
{
    if (!_dialogArt.Sampler.IsValid()) {
        const auto load = [this](const std::string_view name) {
            const std::string   path    = std::format("assets/engine/ui/dialog/{}.png", name);
            const TextureHandle texture = _textures->Load(path);
            if (!texture.IsValid()) LOG_WARN(Engine, "Dialog art '{}' not found", path);
            return texture;
        };
        _dialogArt = { .Box       = load("box"),
                       .NamePlate = load("name_plate"),
                       .NextArrow = load("next_arrow"),
                       .Cursor    = load("cursor"),
                       .Sampler   = _nearestSampler };
    }
    auto screen          = std::make_unique<DialogScreen>();
    screen->Art          = _dialogArt;
    screen->LoadPortrait = [this](const std::string& path) { return _textures->Load(path); };
    screen->PlayVoice    = [](const std::string& path) { AudioManager::PlayOneShot(path); };
    screen->DefaultVoice = "assets/engine/ui/dialog/voice.wav";
    screen->Skin         = &_ui.GetSkin();
    return screen;
}

bool Application::StartDialog(DialogRequest request)
{
    if (!_dialogs.Has(request.Id)) {
        LOG_WARN(Engine, "StartDialog: there's no dialog '{}'", request.Id);
        return false;
    }
    if (!request.Scene) request.Scene = _scenes.GetActive();
    _dialogQueue.push_back(std::move(request));
    if (!_dialog) BeginNextDialog();
    return true;
}

void Application::StopDialog()
{
    _dialogQueue.clear();
    if (!_dialog) return;
    DialogRunner& runner = _dialog->Screen->GetRunner();
    runner.Stop();
    EndDialog(runner.GetLastChoice());
}

// The next waiting conversation, if nothing runs. One whose scene has gone
// meanwhile still runs, with no one in it.
void Application::BeginNextDialog()
{
    while (!_dialog && !_dialogQueue.empty()) {
        DialogRequest request = std::move(_dialogQueue.front());
        _dialogQueue.pop_front();
        auto dialog = _dialogs.Get(request.Id);
        if (!dialog) continue;
        if (!_scenes.Contains(request.Scene)) request = { .Id = request.Id, .Node = request.Node, .Scene = _scenes.GetActive() };

        auto screen = CreateDialogScreen();
        if (_scenes.Contains(request.Scene)) // the key that started the talk moves it on too
            if (const auto* by = request.Scene->GetWorld().GetComponent<Interactor>(request.By))
                screen->AdvanceActions.push_back(MakeAction(by->Action));
        _dialog     = ActiveDialog{ .Request = request, .Screen = screen.get(), .PauseWorld = dialog->PauseWorld };
        HoldDialogParties(request, true);
        WatchDialog(*screen, request);
        _events.PublishEngine(EngineEvents::DialogStarted, { { "id", request.Id }, { "with", request.With } });
        screen->Begin(std::move(dialog), MakeDialogHost(request), request.Node);
        _ui.Push(std::move(screen));
    }
}

// Lines and choices become engine events; the end ends it - if it's still
// this screen's conversation (StopDialog may have ended it already).
void Application::WatchDialog(DialogScreen& screen, const DialogRequest& request)
{
    DialogRunner& runner = screen.GetRunner();
    runner.OnLine        = [this, id = request.Id](const DialogRunner::Line& line) {
        _events.PublishEngine(EngineEvents::DialogLine, { { "id", id },
                                                          { "key", line.Key },
                                                          { "speaker", line.Speaker ? line.Speaker->Id : std::string() } });
    };
    runner.OnChoose = [this, id = request.Id](const std::string& choice) {
        _events.PublishEngine(EngineEvents::DialogChoice, { { "id", id }, { "choice", choice } });
    };
    screen.OnFinished = [this, box = &screen](const std::string& choice) {
        if (_dialog && _dialog->Screen == box) EndDialog(choice);
    };
}

// The next one starts on the next tick (UpdateDialogs), not from inside the
// box's own update.
void Application::EndDialog(const std::string& choice)
{
    const ActiveDialog ended = std::move(*_dialog);
    _dialog.reset();
    _ui.Close(ended.Screen);
    HoldDialogParties(ended.Request, false);
    _events.PublishEngine(EngineEvents::DialogEnded,
                          { { "id", ended.Request.Id }, { "with", ended.Request.With }, { "choice", choice } });
    if (_scenes.Contains(ended.Request.Scene)) ended.Request.Scene->DialogEnded(ended.Request.With, ended.Request.Id, choice);
}

void Application::HoldDialogParties(const DialogRequest& request, const bool hold)
{
    if (!_scenes.Contains(request.Scene)) return; // pointers compared before one is used
    World& world = request.Scene->GetWorld();
    for (const Entity e : { request.With, request.By }) {
        if (!e.IsValid() || !world.IsValid(e)) continue;
        if (hold) Hold(world, e);
        else Release(world, e);
    }
    if (hold && request.With.IsValid() && request.By.IsValid()) TurnToward(world, request.With, request.By);
}

// SaveData and events; Lua in the dialog environment; "call" on With's script.
DialogHost Application::MakeDialogHost(const DialogRequest& request)
{
    const std::string chunk = std::format("dialog {}", request.Id);
    DialogHost        host{ .Data = &_saves.Data(), .Events = &_events };
    host.Test = [this, chunk](const std::string_view expression) { return _scriptEngine.Test(expression, chunk); };
    host.Run  = [this, chunk](const std::string_view code) { (void)_scriptEngine.Execute(code, chunk); };
    host.Call = [this, request](const std::string_view method) {
        const bool called = request.With.IsValid() && _scenes.Contains(request.Scene) &&
                            request.Scene->CallScript(request.With, method);
        if (!called) LOG_WARN(Engine, "Dialog '{}': no '{}' to call on the one talked to", request.Id, method);
    };
    return host;
}

void Application::UpdateDialogs()
{
    if (!_dialog) BeginNextDialog();
}

void Application::RegisterDialogCommands()
{
    _debugConsole.Register({ .Name = "dialog", .Help = "dialog [id] [node] - list the dialogs, or start one", .Cheat = true,
                             .Run = [this](const DebugConsole::Args& a) {
                                 if (a.empty()) {
                                     for (const std::string& id : _dialogs.GetIds()) _debugConsole.Print("  " + id);
                                     return;
                                 }
                                 if (!StartDialog({ .Id = a[0], .Node = a.size() > 1 ? a[1] : "" }))
                                     throw std::runtime_error(std::format("no dialog '{}'", a[0]));
                             } });
}
} // namespace Arcbit
