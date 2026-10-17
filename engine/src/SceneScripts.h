#pragma once

// Engine-internal: the Lua side of one scene - its scripted entities and its
// scene script. Light on purpose (no sol2) so Scene.cpp can hold one.

#include <arcbit/core/Types.h>
#include <arcbit/ecs/Entity.h>

#include <memory>
#include <string>

namespace Arcbit
{
class Scene;
class ScriptEngine;

class SceneScripts
{
public:
    SceneScripts(ScriptEngine& engine, Scene& scene, std::string sceneModule);
    ~SceneScripts(); // drops every instance without calling hooks - End() calls them

    SceneScripts(const SceneScripts&)            = delete;
    SceneScripts& operator=(const SceneScripts&) = delete;

    // The scene script's OnLoad, then OnStart for every Script already there.
    void Start();

    // Start Scripts added since (spawned entities), then OnUpdate: the scene
    // script's, then each entity's.
    void Update(f32 dt);

    // The scene is unloading: every entity's OnDestroy, then the scene
    // script's OnUnload. Instances are dropped; handles to the scene go stale.
    void End();

    // A Script left its entity (removed, or the entity destroyed): OnDestroy.
    void Removed(Entity entity);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace Arcbit
