#include <arcbit/scene/Scene.h>

#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/audio/MusicPlayer.h>
#include <arcbit/core/Assert.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/save/SceneState.h>
#include <arcbit/script/Script.h>

#include "SceneScripts.h"
#include <arcbit/render/RenderThread.h>

#include <utility>

namespace Arcbit {

// Defined in Systems.cpp — registers all built-in ECS systems.
void RegisterBuiltinSystems(World& world);

Scene::Scene()
{
    RegisterBuiltinSystems(_world);

    // Every way a collider leaves (DestroyEntity, RemoveComponent) also drops
    // its broadphase record — otherwise it keeps blocking movers and
    // overlapping triggers after the entity is gone.
    _world.OnRemove<Collider2D>([this](const Entity e, Collider2D&) {
        if (!_physics) return;
        if (const PhysicsWorld::ColliderId id = _physics->FindColliderForEntity(e); id != PhysicsWorld::InvalidId)
            _physics->UnregisterCollider(id);
    });

    // A sound outlives nothing: removing the source (or its entity, or the
    // whole scene) stops it. Without this a looping sound kept playing after
    // its scene was unloaded.
    _world.OnRemove<AudioSource>([](Entity, AudioSource& src) {
        if (!src._handle) return;
        AudioManager::DestroySpatialSound(src._handle);
        src._handle = nullptr;
    });

    HookEntityEvents();

    // A Script leaving its entity (removed, or the entity destroyed) runs its
    // OnDestroy - but not as the scene itself is torn down: EndScripts did that.
    _world.OnRemove<Script>([this](const Entity e, Script&) {
        if (_scripts && !_tearingDown) _scripts->Removed(e);
    });
}

Scene::~Scene() // out-of-line: SpriteSheet and PhysicsWorld are complete here
{
    // Run the OnRemove hooks while the members they use are still alive.
    _tearingDown = true;
    _world.DestroyAllEntities();
}

// Opt-in entity events (EntityEvents). Spawned waits for the next Update:
// the component is usually added mid-creation, before the entity's Name.
void Scene::HookEntityEvents()
{
    _world.OnAdd<EntityEvents>([this](const Entity e, const EntityEvents& ev) {
        if (ev.Spawned) _pendingSpawns.push_back(e);
    });
    _world.OnDestroy([this](const Entity e) {
        if (_tearingDown) return;
        const EntityEvents* ev = _world.GetComponent<EntityEvents>(e);
        if (ev && ev->Destroyed) PublishEntityEvent(e, ev->DestroyedEvent, EngineEvents::EntityDestroyed);
    });
}

void Scene::PublishPendingSpawns()
{
    for (const Entity e : std::exchange(_pendingSpawns, {})) {
        if (!_world.IsValid(e)) continue; // came and went within a tick
        const EntityEvents* ev = _world.GetComponent<EntityEvents>(e);
        if (ev && ev->Spawned) PublishEntityEvent(e, ev->SpawnedEvent, EngineEvents::EntitySpawned);
    }
}

void Scene::PublishEntityEvent(const Entity entity, const std::string& rename, const EventId fallback)
{
    if (!_events) return;
    const Name*  name = _world.GetComponent<Name>(entity);
    const Tag*   tag  = _world.GetComponent<Tag>(entity);
    EventPayload payload{ { "entity", entity },
                          { "name", name ? name->Value : std::string{} },
                          { "tag", tag ? tag->Value : std::string{} } };
    if (rename.empty()) _events->PublishEngine(fallback, std::move(payload));
    else _events->Publish(rename, std::move(payload));
}

Application& Scene::GetApp()
{
    ARCBIT_ASSERT(_app != nullptr, "Scene::GetApp: scene isn't hosted by an Application's SceneManager");
    return *_app;
}

void Scene::SetMusic(std::string path, const bool loop)
{
    if (path != _musicPath) _musicResume = 0.0f; // the saved spot belonged to the old track
    _ownsMusic = true;
    _musicPath = std::move(path);
    _musicLoop = loop;
    if (!_musicPlayer) return; // not active: plays when it is
    if (_musicPath.empty()) _musicPlayer->Stop(_musicFade);
    else _musicPlayer->Play(_musicPath, _musicLoop, _musicFade, 0.0f);
}

TextureHandle Scene::LoadTexture(const std::string_view path)
{
    const TextureHandle handle = GetApp().GetTextures().Acquire(path);
    if (handle.IsValid()) _textures.push_back(handle);
    return handle;
}

const SpriteSheet* Scene::LoadSpriteSheet(const std::string_view path)
{
    const std::string key(path);
    if (const auto it = _sheets.find(key); it != _sheets.end()) return it->second.get();

    auto sheet = std::make_unique<SpriteSheet>(SpriteSheet::Load(
        path, GetApp().GetTextures(), [this](const std::string_view texPath) { return LoadTexture(texPath); }));
    if (!sheet->IsValid()) return nullptr;
    return _sheets.emplace(key, std::move(sheet)).first->second.get();
}

std::string Scene::GetSpriteSheetPath(const SpriteSheet* sheet) const
{
    for (const auto& [path, owned] : _sheets)
        if (owned.get() == sheet) return path;
    return {};
}

void Scene::StopSounds()
{
    _world.Query<AudioSource>().ForEach([](AudioSource& src) {
        if (!src._handle) return;
        AudioManager::DestroySpatialSound(src._handle);
        src._handle = nullptr;
    });
}

void Scene::Subscribe(const EventId id, EventHandler handler)
{
    ARCBIT_ASSERT(_events != nullptr, "Scene::Subscribe needs a scene hosted by a SceneManager with an event bus");
    if (_events) _subscriptions.push_back(_events->Subscribe(id, std::move(handler)));
}

void Scene::StartScripts()
{
    if (!_scriptEngine || _scripts) return;
    _scripts = std::make_unique<SceneScripts>(*_scriptEngine, *this, _scriptModule);
    _scripts->Start();
}

void Scene::EndScripts()
{
    if (!_scripts) return;
    _scripts->End();
    _scripts.reset();
}

void Scene::SyncScriptState()
{
    if (_scripts) _scripts->SyncSaved();
}

// Interactors are listed first and used after: OnInteract may change the world.
void Scene::UpdateInteraction()
{
    if (!_input || _inputBlocked) return;
    std::vector<Entity> pressed;
    _world.Query<Interactor>().ForEach([&](const Entity e, const Interactor& interactor) {
        if (_input->JustPressed(MakeAction(interactor.Action))) pressed.push_back(e);
    });
    for (const Entity by : pressed) {
        if (!_world.IsValid(by)) continue;
        const Entity target = FindInteractTarget(_world, by, _config.TileSize);
        if (!target.IsValid()) continue;
        if (_scripts) _scripts->Interact(target, by);
        if (_events) _events->PublishEngine(EngineEvents::EntityInteracted, { { "entity", target }, { "by", by } });
    }
}

void Scene::ReleaseResources()
{
    if (_app)
        for (const TextureHandle h : _textures) _app->GetTextures().Release(h);
    _textures.clear();
}

PhysicsWorld& Scene::GetPhysics()
{
    if (!_physics) {
        _physics = std::make_unique<PhysicsWorld>(_config.TileSize);
        _physics->SetTileMap(&_tileMap);
    }
    return *_physics;
}

void Scene::Update(const f32 dt)
{
    PublishPendingSpawns();
    OnUpdate(dt);
    UpdateInteraction();
    if (_scripts) _scripts->Update(dt);
    _camera.Update(dt);
    _world.RunUpdateSystems(*this, dt);
}

void Scene::CollectRenderData(FramePacket& packet)
{
    packet.CameraPosition = _camera.GetEffectivePosition();
    packet.CameraZoom     = _camera.Zoom;
    packet.CameraRotation = _camera.Rotation;
    OnRender(packet);
    _world.RunRenderSystems(*this, packet);
}

} // namespace Arcbit
