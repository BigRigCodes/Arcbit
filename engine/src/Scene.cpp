#include <arcbit/scene/Scene.h>
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
}

Scene::~Scene() = default; // out-of-line for unique_ptr<PhysicsWorld> destruction

PhysicsWorld& Scene::GetPhysics()
{
    if (!_physics) {
        _physics = std::make_unique<PhysicsWorld>(_config.TileSize);
        _physics->SetTileMap(&_tileMap);
    }
    return *_physics;
}

void Scene::Update(f32 dt)
{
    _camera.Update(dt);
    _world.RunUpdateSystems(*this, dt);
}

void Scene::CollectRenderData(FramePacket& packet)
{
    packet.CameraPosition = _camera.GetEffectivePosition();
    packet.CameraZoom     = _camera.Zoom;
    packet.CameraRotation = _camera.Rotation;
    _world.RunRenderSystems(*this, packet);
}

} // namespace Arcbit
