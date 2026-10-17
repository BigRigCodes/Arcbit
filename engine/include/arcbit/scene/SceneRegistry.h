#pragma once

#include <arcbit/ecs/World.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Scene;
class SceneAssets;

// ---------------------------------------------------------------------------
// SceneRegistry - what scene files may contain: components by name (with
// their JSON read / write functions) and named systems.
//
// The engine registers its built-in components (RegisterBuiltinComponents);
// a game registers its own the same way. A scene file can't hold C++, so game
// behavior is registered as a named system and listed in the file. See
// docs/scenes.md.
// ---------------------------------------------------------------------------
class SceneRegistry
{
public:
    // Fill a component from its JSON object. Missing keys keep the
    // component's current values (defaults, for a new component).
    template<typename T>
    using ReadFn = std::function<void(T&, const nlohmann::json&, SceneAssets&)>;

    // Write every field of a component. The registry drops the ones equal to
    // a default-constructed T, so writers don't have to.
    template<typename T>
    using WriteFn = std::function<void(const T&, nlohmann::json&, const SceneAssets&)>;

    // Runs once when a scene that lists the system loads, before its entities
    // are created; typically registers update or render systems on the
    // scene's world.
    using InstallFn = std::function<void(Scene&)>;

    struct ComponentEntry
    {
        std::string                                                                Name;
        std::function<bool(World&, Entity)>                                        Has;
        std::function<void(World&, Entity, const nlohmann::json&, SceneAssets&)>   Read;  // adds it if absent
        std::function<nlohmann::json(World&, Entity, const SceneAssets&)>          Write; // non-default fields
    };

    // Register (or replace) the component `name` as type T.
    template<typename T>
    void RegisterComponent(std::string name, ReadFn<T> read, WriteFn<T> write);

    // Register (or replace) the named system `name`.
    void RegisterSystem(std::string name, InstallFn install);

    [[nodiscard]] const ComponentEntry* FindComponent(std::string_view name) const;
    [[nodiscard]] const InstallFn*      FindSystem(std::string_view name) const;

    // Every registered component, in registration order.
    [[nodiscard]] const std::vector<ComponentEntry>& Components() const { return _components; }

private:
    std::vector<ComponentEntry>                         _components;
    std::vector<std::pair<std::string, InstallFn>>      _systems;

    void Add(ComponentEntry entry);
};

// Remove keys of `fields` whose values equal the same key in `defaults`.
void DropDefaults(nlohmann::json& fields, const nlohmann::json& defaults);

// Register the engine's own components (Transform2D, SpriteRenderer,
// Collider2D, the movement components, ...). Application does this for its
// registry; tests call it on their own.
void RegisterBuiltinComponents(SceneRegistry& registry);

template<typename T>
void SceneRegistry::RegisterComponent(std::string name, ReadFn<T> read, WriteFn<T> write)
{
    ComponentEntry entry;
    entry.Name = std::move(name);
    entry.Has  = [](World& w, const Entity e) { return w.GetComponent<T>(e) != nullptr; };
    entry.Read = [read](World& w, const Entity e, const nlohmann::json& j, SceneAssets& assets) {
        if (T* existing = w.GetComponent<T>(e)) {
            read(*existing, j, assets);
            return;
        }
        T value{};
        read(value, j, assets);
        w.AddComponent<T>(e, std::move(value));
    };
    entry.Write = [write](World& w, const Entity e, const SceneAssets& assets) {
        nlohmann::json fields   = nlohmann::json::object();
        nlohmann::json defaults = nlohmann::json::object();
        write(*w.GetComponent<T>(e), fields, assets);
        write(T{}, defaults, assets);
        DropDefaults(fields, defaults);
        return fields;
    };
    Add(std::move(entry));
}
} // namespace Arcbit
