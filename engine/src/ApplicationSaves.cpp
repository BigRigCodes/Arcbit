// Saving and loading the game: the engine's own save sections, SaveTo /
// LoadFrom and starting over. Split out of Application.cpp, which owns the
// main loop.

#include <arcbit/app/Application.h>

#include <arcbit/core/Log.h>
#include <arcbit/events/EngineEvents.h>

#include <chrono>
#include <memory>

namespace Arcbit
{
namespace
{
    using json = nlohmann::json;

    // The "scenes" section: { "stack": [{ "name", "entry" }] }, bottom first.
    json StackToJson(const std::vector<SceneStackEntry>& stack)
    {
        json entries = json::array();
        for (const SceneStackEntry& entry : stack) entries.push_back({ { "name", entry.Name }, { "entry", entry.Entry } });
        return { { "stack", entries } };
    }

    std::vector<SceneStackEntry> StackFromJson(const json& section)
    {
        std::vector<SceneStackEntry> stack;
        const auto entries = section.find("stack");
        if (entries == section.end() || !entries->is_array()) return stack;
        for (const json& entry : *entries)
            if (entry.is_object() && entry.contains("name") && entry["name"].is_string())
                stack.push_back({ entry["name"].get<std::string>(), entry.value("entry", std::string{}) });
        return stack;
    }

    i64 UnixNow()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>(now).count();
    }
} // namespace

void Application::RegisterSaveSections()
{
    // Every visited scene's changes - the live scenes' captured first, since
    // they haven't unloaded.
    _saves.RegisterParticipant(
        "world",
        [this](json& section) {
            _scenes.CaptureLive();
            section = _world.ToJson();
        },
        [this](const json& section) { _world.FromJson(section); });

    // The scene stack. LoadFrom reads it before anything is restored (it
    // decides what loads), so it has no read callback.
    _saves.RegisterParticipant("scenes", [this](json& section) { section = StackToJson(_scenes.GetStack()); }, {});
}

bool Application::SaveTo(const SaveSlot slot, std::string name)
{
    const Scene* active = _scenes.GetActive();
    if (_scenes.IsTransitioning() || !active || active->GetName().empty()) {
        LOG_WARN(Engine, "Can't save to '{}' now - {}", ToString(slot),
                 _scenes.IsTransitioning() ? "a scene change is under way" : "no scene is loaded");
        return false;
    }
    SaveMetadata meta{ .Name     = name.empty() ? active->GetName() : std::move(name),
                       .Scene    = active->GetName(),
                       .SavedAt  = UnixNow(),
                       .Playtime = _playtime };
    std::string label = meta.Name;
    if (!_saves.Write(slot, _saves.Capture(std::move(meta)))) return false;
    _events.PublishEngine(EngineEvents::GameSaved, { { "slot", ToString(slot) }, { "name", std::move(label) } });
    return true;
}

// The save is read now, but nothing changes until the swap: the old scenes
// unload (their changes go to the old world state), then the save's state
// replaces it, then its scenes load with that state applied.
bool Application::LoadFrom(const SaveSlot slot)
{
    if (_scenes.IsTransitioning()) {
        LOG_WARN(Engine, "Can't load '{}' now - a scene change is under way", ToString(slot));
        return false;
    }
    std::optional<SaveGame> game = _saves.Read(slot);
    if (!game) return false; // SaveManager logged why

    const auto                   section = game->Sections.find("scenes");
    std::vector<SceneStackEntry> stack   = StackFromJson(section != game->Sections.end() ? *section : json::object());
    if (stack.empty()) {
        LOG_WARN(Engine, "Save '{}' has no scenes to load", ToString(slot));
        return false;
    }
    auto loaded = std::make_shared<const SaveGame>(std::move(*game));
    return _scenes.Restore(std::move(stack), [this, loaded, slot] {
        _saves.Restore(*loaded);
        _playtime = loaded->Meta.Playtime;
        _events.PublishEngine(EngineEvents::GameLoaded, { { "slot", ToString(slot) } });
        LOG_INFO(Engine, "Loaded '{}' ({:.0f} s played)", ToString(slot), _playtime);
    });
}

// An empty save restored: SaveData cleared and every participant handed an
// empty section, which clears the world state too.
void Application::ResetGame()
{
    _saves.Restore(SaveGame{});
    _playtime = 0.0;
}
} // namespace Arcbit
