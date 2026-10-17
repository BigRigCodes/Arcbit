#include <arcbit/scene/Scene.h>
#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Assert.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/RenderThread.h>

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
}

Scene::~Scene() // out-of-line: SpriteSheet and PhysicsWorld are complete here
{
    // Run the OnRemove hooks while the members they use are still alive.
    _world.DestroyAllEntities();
}

Application& Scene::GetApp()
{
    ARCBIT_ASSERT(_app != nullptr, "Scene::GetApp: scene isn't hosted by an Application's SceneManager");
    return *_app;
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
    OnUpdate(dt);
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
