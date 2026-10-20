// Scene, Tilemap, Input and Audio for scripts - each acting on the context
// scene (the hook's, or the active one from the console).

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneManager.h>

#include <format>
#include <tuple>

namespace Arcbit
{
namespace
{
    SceneManager& Manager(const ScriptServices& services)
    {
        if (!services.Scenes) throw std::runtime_error("Scene: no scene manager");
        return *services.Scenes;
    }

    void RequireRegistered(const SceneManager& scenes, const std::string& name)
    {
        if (!scenes.IsRegistered(name)) throw std::runtime_error(std::format("Scene: no scene registered as '{}'", name));
    }
} // namespace

// Scene.GetName / GetEntry / Switch / Push / Pop.
void ScriptEngine::State::BindScene()
{
    sol::table scene = Lua.create_table();
    scene.set_function("GetName", [this] { return RequireScene().GetName(); });
    scene.set_function("GetEntry", [this] { return RequireScene().GetEntryPoint(); });
    scene.set_function("Switch", [this](const std::string& name, const sol::optional<std::string> entry) {
        SceneManager& scenes = Manager(Services);
        RequireRegistered(scenes, name);
        scenes.Switch(name, entry.value_or(""));
    });
    scene.set_function("Push", [this](const std::string& name, const sol::optional<std::string> entry) {
        SceneManager& scenes = Manager(Services);
        RequireRegistered(scenes, name);
        scenes.Push(name, entry.value_or(""));
    });
    scene.set_function("Pop", [this] { Manager(Services).Pop(); });
    AddGlobal("Scene", scene);
}

// Tilemap.GetTile / SetTile / IsSolid / WorldToTile / TileToWorld - the same
// argument order as TileMap (x, y, layer, id).
void ScriptEngine::State::BindTilemap()
{
    sol::table tilemap = Lua.create_table();
    tilemap.set_function("GetTile", [this](const i32 x, const i32 y, const sol::optional<u32> layer) {
        return RequireScene().GetTileMap().GetTile(x, y, layer.value_or(0));
    });
    tilemap.set_function("SetTile", [this](const i32 x, const i32 y, const u32 layer, const u32 id) {
        if (layer >= LayerCount) throw std::runtime_error(std::format("Tilemap.SetTile: no layer {}", layer));
        RequireScene().GetTileMap().SetTile(x, y, layer, id);
    });
    tilemap.set_function("IsSolid", [this](const i32 x, const i32 y) { return RequireScene().GetTileMap().IsSolid(x, y); });
    tilemap.set_function("WorldToTile", [this](const Vec2 position) {
        i32 x = 0, y = 0;
        RequireScene().GetTileMap().WorldToTile(position, x, y);
        return std::make_tuple(x, y);
    });
    tilemap.set_function("TileToWorld", [this](const i32 x, const i32 y) { return RequireScene().GetTileMap().TileToWorld(x, y); });
    AddGlobal("Tilemap", tilemap);
}

// Input.IsDown / IsPressed / IsReleased by action name. False while the
// player is held still (a menu is open, the screen is fading).
void ScriptEngine::State::BindInput()
{
    const auto query = [this](const std::string& action, bool (InputManager::*test)(ActionID) const) {
        if (!Services.Input) return false;
        if (const Scene* scene = ContextScene(); scene && scene->IsInputBlocked()) return false;
        return (Services.Input->*test)(MakeAction(action));
    };
    sol::table input = Lua.create_table();
    input.set_function("IsDown", [query](const std::string& action) { return query(action, &InputManager::IsPressed); });
    input.set_function("IsPressed", [query](const std::string& action) { return query(action, &InputManager::JustPressed); });
    input.set_function("IsReleased", [query](const std::string& action) { return query(action, &InputManager::JustReleased); });
    AddGlobal("Input", input);
}

// Audio.PlaySound(path, volume, category), and music through the context
// scene's SetMusic so it follows the scene like music set from C++.
void ScriptEngine::State::BindAudio()
{
    sol::table audio = Lua.create_table();
    audio.set_function("PlaySound", [this](const std::string& path, const sol::optional<f32> volume,
                                           const sol::optional<std::string> category) {
        if (Services.PlaySound) Services.PlaySound(path, volume.value_or(1.0f), category.value_or("sfx"));
    });
    audio.set_function("PlayMusic", [this](const std::string& path, const sol::optional<bool> loop,
                                           const sol::optional<std::string> category) {
        RequireScene().SetMusic(path, loop.value_or(true), category.value_or("music"));
    });
    audio.set_function("StopMusic", [this] { RequireScene().SetMusic(""); });
    AddGlobal("Audio", audio);
}
} // namespace Arcbit
