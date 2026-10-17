#include <arcbit/ecs/World.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Internal — component type registry
// ---------------------------------------------------------------------------
namespace Internal {

static std::vector<std::function<ComponentArray()>>& GetRegistry()
{
    static std::vector<std::function<ComponentArray()>> r;
    return r;
}

ComponentTypeID AllocID()
{
    GetRegistry().emplace_back();
    return static_cast<ComponentTypeID>(GetRegistry().size() - 1);
}

void RegisterFactory(const ComponentTypeID id, std::function<ComponentArray()> factory)
{
    GetRegistry()[id] = std::move(factory);
}

ComponentArray CreateArray(const ComponentTypeID id)
{
    ARCBIT_ASSERT(id < GetRegistry().size() && GetRegistry()[id], "CreateArray: unknown component type");
    return GetRegistry()[id]();
}

} // namespace Internal

// ---------------------------------------------------------------------------
// ComponentArray
// ---------------------------------------------------------------------------

ComponentArray::~ComponentArray() { DestroyAll(); }

ComponentArray::ComponentArray(ComponentArray&& other) noexcept
    : _data(std::move(other._data)),
      _elementSize(other._elementSize),
      _count(std::exchange(other._count, 0u)),
      _capacity(std::exchange(other._capacity, 0u)),
      _defaultConstruct(std::move(other._defaultConstruct)),
      _destruct(std::move(other._destruct)),
      _moveConstruct(std::move(other._moveConstruct))
{}

ComponentArray& ComponentArray::operator=(ComponentArray&& other) noexcept
{
    if (this == &other) return *this;
    DestroyAll();
    _data             = std::move(other._data);
    _elementSize      = other._elementSize;
    _count            = std::exchange(other._count, 0u);
    _capacity         = std::exchange(other._capacity, 0u);
    _defaultConstruct = std::move(other._defaultConstruct);
    _destruct         = std::move(other._destruct);
    _moveConstruct    = std::move(other._moveConstruct);
    return *this;
}

void ComponentArray::DestroyAll() noexcept
{
    for (u32 i = 0; i < _count; ++i) _destruct(GetRaw(i));
    _count = 0;
}

void* ComponentArray::AppendSlot()
{
    if (_count == _capacity) Reallocate(std::max(8u, _capacity * 2u));
    return GetRaw(_count);
}

void ComponentArray::Reallocate(const u32 newCapacity)
{
    std::vector<u8> fresh(static_cast<usize>(newCapacity) * _elementSize);
    for (u32 i = 0; i < _count; ++i) {
        u8* dst = fresh.data() + static_cast<usize>(i) * _elementSize;
        _moveConstruct(dst, GetRaw(i));
        _destruct(GetRaw(i));
    }
    _data.swap(fresh);
    _capacity = newCapacity;
}

void ComponentArray::PushDefault()
{
    _defaultConstruct(AppendSlot());
    ++_count;
}

void ComponentArray::MoveFrom(const u32 srcIndex, ComponentArray& src)
{
    _moveConstruct(AppendSlot(), src.GetRaw(srcIndex));
    ++_count;
}

void ComponentArray::SwapRemove(const u32 index)
{
    ARCBIT_ASSERT(index < _count, "SwapRemove: index out of range");
    const u32 last = _count - 1;
    if (index != last) {
        _destruct(GetRaw(index));
        _moveConstruct(GetRaw(index), GetRaw(last));
    }
    _destruct(GetRaw(last));
    --_count; // slot stays allocated for reuse
}

// ---------------------------------------------------------------------------
// World — private helpers
// ---------------------------------------------------------------------------

Archetype& World::GetOrCreateArchetype(ComponentMask mask)
{
    auto it = _archetypes.find(mask);
    if (it != _archetypes.end())
        return it->second;

    Archetype& arch = _archetypes.emplace(mask, Archetype{}).first->second;
    arch.Mask = mask;
    for (ComponentTypeID i = 0; i < 64; ++i)
        if (mask & (1ULL << i))
            arch.Columns.emplace(i, Internal::CreateArray(i));
    return arch;
}

void World::SwapRemoveFromArchetype(Archetype& arch, const u32 slot)
{
    const u32 lastSlot = static_cast<u32>(arch.Entities.size()) - 1;

    for (auto& [id, col] : arch.Columns)
        col.SwapRemove(slot);

    if (slot != lastSlot) {
        arch.Entities[slot] = arch.Entities[lastSlot];
        _entityTable[arch.Entities[slot].Index].slot = slot;
    }
    arch.Entities.pop_back();
}

// ---------------------------------------------------------------------------
// World — public API
// ---------------------------------------------------------------------------

Entity World::CreateEntity()
{
    ARCBIT_ASSERT(!IsIterating(), "CreateEntity: called inside ForEach - create after it returns");
    const ComponentTypeID transformID = TypeOf<Transform2D>();
    const ComponentTypeID tagID       = TypeOf<Tag>();
    const ComponentMask   mask        = (1ULL << transformID) | (1ULL << tagID);

    Archetype& arch = GetOrCreateArchetype(mask);

    u32 index, generation;
    if (!_freeList.empty()) {
        index      = _freeList.back();
        generation = _entityTable[index].generation;
        _freeList.pop_back();
    } else {
        index      = static_cast<u32>(_entityTable.size());
        generation = 0;
        _entityTable.push_back({});
    }

    const Entity entity = { index, generation };
    const u32    slot   = static_cast<u32>(arch.Entities.size());

    for (auto& col : arch.Columns | std::views::values)
        col.PushDefault();
    arch.Entities.push_back(entity);

    _entityTable[index] = { mask, slot, generation };
    return entity;
}

void World::DestroyEntity(const Entity entity)
{
    ARCBIT_ASSERT(!IsIterating(), "DestroyEntity: called inside ForEach - collect the entities, destroy after");
    if (!IsValid(entity)) return;
    for (const auto& hook : _destroyHooks) hook(entity);
    for (const ComponentTypeID type : _removeHooks | std::views::keys)
        if (_entityTable[entity.Index].mask & (1ULL << type)) RunRemoveHooks(entity, type);

    auto& rec = _entityTable[entity.Index];
    SwapRemoveFromArchetype(_archetypes.at(rec.mask), rec.slot);
    ++rec.generation;
    rec.mask = 0;
    rec.slot = 0;
    _freeList.push_back(entity.Index);
}

void World::DestroyAllEntities()
{
    // A free slot's record already carries its next generation, so a handle
    // built from it would pass IsValid; an empty mask is what marks it free
    // (live entities always have Transform2D + Tag).
    for (u32 i = 0; i < static_cast<u32>(_entityTable.size()); ++i)
        if (_entityTable[i].mask != 0) DestroyEntity(Entity{ i, _entityTable[i].generation });
}

void World::RunAddHooks(const Entity entity, const ComponentTypeID type)
{
    const auto it = _addHooks.find(type);
    if (it == _addHooks.end()) return;
    const EntityRecord& rec       = _entityTable[entity.Index];
    void*               component = _archetypes.at(rec.mask).Columns.at(type).GetRaw(rec.slot);
    for (const RemoveHook& hook : it->second) hook(entity, component);
}

void World::RunRemoveHooks(const Entity entity, const ComponentTypeID type)
{
    const auto it = _removeHooks.find(type);
    if (it == _removeHooks.end()) return;
    const EntityRecord& rec       = _entityTable[entity.Index];
    void*               component = _archetypes.at(rec.mask).Columns.at(type).GetRaw(rec.slot);
    for (const RemoveHook& hook : it->second) hook(entity, component);
}

bool World::IsValid(const Entity entity) const
{
    if (entity.Index == ~0u || entity.Index >= _entityTable.size())
        return false;
    return _entityTable[entity.Index].generation == entity.Generation;
}

void World::RegisterSystem(const std::string_view name, UpdateFn fn)
{
    _updateSystems.emplace_back(std::string(name), std::move(fn));
}

void World::RegisterRenderSystem(const std::string_view name, RenderFn fn)
{
    _renderSystems.emplace_back(std::string(name), std::move(fn));
}

void World::RunUpdateSystems(Scene& scene, const f32 dt)
{
    for (auto& fn : _updateSystems | std::views::values)
        fn(scene, dt);
}

void World::RunRenderSystems(Scene& scene, FramePacket& packet)
{
    for (auto& fn : _renderSystems | std::views::values)
        fn(scene, packet);
}

Transform2D* World::GetTransform(const Entity entity) { return GetComponent<Transform2D>(entity); }

} // namespace Arcbit
