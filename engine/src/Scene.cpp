#include <arcbit/scene/Scene.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/RenderThread.h>

namespace Arcbit {

// Defined in Systems.cpp — registers all built-in ECS systems.
void RegisterBuiltinSystems(World& world);

Scene::Scene()
{
    RegisterBuiltinSystems(_world);
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
