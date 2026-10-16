#pragma once

#include <arcbit/ecs/AnimatorStateMachine.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Types.h>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Arcbit
{

class Scene;
struct FramePacket;

// ---------------------------------------------------------------------------
// Entity — generational index that prevents use-after-free.
// ---------------------------------------------------------------------------
struct Entity
{
    u32 Index      = ~0u;
    u32 Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != ~0u; }

    bool operator==(const Entity& o) const { return Index == o.Index && Generation == o.Generation; }
    bool operator!=(const Entity& o) const { return !(*this == o); }

    static Entity Invalid() { return {}; }
};

// ---------------------------------------------------------------------------
// Component type IDs — assigned once per type at first use, no macros/RTTI.
// ---------------------------------------------------------------------------
using ComponentTypeID = u32;
using ComponentMask   = u64; // caps at 64 distinct component types

// ---------------------------------------------------------------------------
// ComponentArray — type-erased, contiguous storage for one component type.
// Must be defined before Internal namespace so declarations reference the real type.
// ---------------------------------------------------------------------------
//
// Elements are only ever relocated through their move constructor, never by
// copying bytes: components may hold types that point into themselves (MSVC's
// std::function keeps a pointer into its own small-buffer storage), and a
// byte-wise relocation leaves those pointers dangling. The array owns its
// elements — destroying or move-assigning over it runs their destructors.
class ComponentArray
{
public:
    ComponentArray() = default;
    ~ComponentArray();

    ComponentArray(ComponentArray&& other) noexcept;
    ComponentArray& operator=(ComponentArray&& other) noexcept;
    ComponentArray(const ComponentArray&)            = delete;
    ComponentArray& operator=(const ComponentArray&) = delete;

    // Create a typed array. T must be move-constructible and destructible.
    template<typename T>
    static ComponentArray Make()
    {
        // The buffer comes from plain operator new, so over-aligned types
        // would land misaligned.
        static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__,
                      "ComponentArray: over-aligned component types are not supported");
        ComponentArray a;
        a._elementSize      = sizeof(T);
        a._defaultConstruct = [](void* p) { new (p) T{}; };
        a._destruct         = [](void* p) { static_cast<T*>(p)->~T(); };
        a._moveConstruct    = [](void* dst, void* src) { new (dst) T(std::move(*static_cast<T*>(src))); };
        return a;
    }

    // Append a default-constructed element.
    void PushDefault();

    // Append a value (move-constructs in place).
    template<typename T>
    void Push(T&& val)
    {
        new (AppendSlot()) std::remove_cvref_t<T>(std::forward<T>(val));
        ++_count;
    }

    // Append by move-constructing from src[srcIndex].
    // src[srcIndex] remains in a valid moved-from state; caller must SwapRemove it.
    void MoveFrom(u32 srcIndex, ComponentArray& src);

    // Swap-and-pop: replaces element at index with the last, then shrinks by one.
    void SwapRemove(u32 index);

    void* GetRaw(const u32 index) { return _data.data() + index * _elementSize; }

    template<typename T>
    T& Get(const u32 index)
    { return *static_cast<T*>(GetRaw(index)); }

    [[nodiscard]] constexpr u32 Count() const { return _count; }

private:
    // Address of the first unconstructed slot (index _count), growing the
    // buffer first if it is full.
    void* AppendSlot();

    // Move every live element into a fresh buffer of `newCapacity` slots.
    void Reallocate(u32 newCapacity);

    void DestroyAll() noexcept;

    std::vector<u8> _data;            // raw slot storage, always _capacity * _elementSize bytes
    usize           _elementSize = 0;
    u32             _count       = 0; // constructed elements
    u32             _capacity    = 0; // allocated slots

    std::function<void(void*)>        _defaultConstruct;
    std::function<void(void*)>        _destruct;
    std::function<void(void*, void*)> _moveConstruct;
};

// ---------------------------------------------------------------------------
// Internal — component type registry (defined in World.cpp).
// ---------------------------------------------------------------------------
namespace Internal
{
ComponentTypeID AllocID();
void            RegisterFactory(ComponentTypeID id, std::function<ComponentArray()> factory);
ComponentArray  CreateArray(ComponentTypeID id);
} // namespace Internal

// ---------------------------------------------------------------------------
// TypeOf<T> — returns a stable ComponentTypeID for T within this process.
// Registers a ComponentArray factory on the first call for each T.
// ---------------------------------------------------------------------------
template<typename T>
ComponentTypeID TypeOf()
{
    static const ComponentTypeID id = []() {
        const ComponentTypeID newId = Internal::AllocID();
        Internal::RegisterFactory(newId, [] { return ComponentArray::Make<T>(); });
        return newId;
    }();
    return id;
}

// ---------------------------------------------------------------------------
// Archetype — owns all entities with an identical component set.
// ---------------------------------------------------------------------------
struct Archetype
{
    ComponentMask                                       Mask = 0;
    std::unordered_map<ComponentTypeID, ComponentArray> Columns;
    std::vector<Entity>                                 Entities;
};

// Forward-declare QueryView so World::Query() can return it.
template<typename... Ts>
class QueryView;

// ---------------------------------------------------------------------------
// World — owns all archetypes and the entity table.
// ---------------------------------------------------------------------------
class World
{
public:
    using UpdateFn = std::function<void(Scene&, f32)>;
    using RenderFn = std::function<void(Scene&, FramePacket&)>;

    // Create a new entity. Starts with Transform2D + Tag at default values.
    Entity CreateEntity();

    // Destroy an entity. Bumps its generation so stale handles are rejected.
    void DestroyEntity(Entity entity);

    // Returns true if the entity is alive and its generation matches the table.
    [[nodiscard]] bool IsValid(Entity entity) const;

    // Add a component. Moves the entity to a larger archetype. Asserts if already present.
    template<typename T>
    void AddComponent(const Entity entity, T value = {})
    {
        ARCBIT_ASSERT(IsValid(entity), "AddComponent: invalid entity");
        auto&                 rec = _entityTable[entity.Index];
        const ComponentTypeID tid = TypeOf<T>();
        ARCBIT_ASSERT(!(rec.mask & (1ULL << tid)), "AddComponent: component already present");

        const ComponentMask newMask = rec.mask | (1ULL << tid);
        Archetype&          newArch = GetOrCreateArchetype(newMask);
        Archetype&          oldArch = _archetypes.at(rec.mask);
        const u32           oldSlot = rec.slot;

        for (auto& [id, col] : newArch.Columns)
        {
            if (id == tid)
                col.Push(std::move(value));
            else
                col.MoveFrom(oldSlot, oldArch.Columns.at(id));
        }

        const u32 newSlot = static_cast<u32>(newArch.Entities.size());
        newArch.Entities.push_back(entity);
        rec = { newMask, newSlot, rec.generation };
        SwapRemoveFromArchetype(oldArch, oldSlot);
    }

    // Remove a component. Moves to a smaller archetype. Asserts if not present.
    template<typename T>
    void RemoveComponent(const Entity entity)
    {
        ARCBIT_ASSERT(IsValid(entity), "RemoveComponent: invalid entity");
        auto& rec = _entityTable[entity.Index];
        ARCBIT_ASSERT(rec.mask & (1ULL << TypeOf<T>()), "RemoveComponent: component not present");

        const ComponentMask newMask = rec.mask & ~(1ULL << TypeOf<T>());
        Archetype&          newArch = GetOrCreateArchetype(newMask);
        Archetype&          oldArch = _archetypes.at(rec.mask);
        const u32           oldSlot = rec.slot;

        for (auto& [id, col] : newArch.Columns)
            col.MoveFrom(oldSlot, oldArch.Columns.at(id));

        const u32 newSlot = static_cast<u32>(newArch.Entities.size());
        newArch.Entities.push_back(entity);
        rec = { newMask, newSlot, rec.generation };
        SwapRemoveFromArchetype(oldArch, oldSlot);
    }

    // Returns a pointer to the component, or nullptr if not present or entity invalid.
    template<typename T>
    T* GetComponent(const Entity entity)
    {
        if (!IsValid(entity))
            return nullptr;
        const auto&           rec = _entityTable[entity.Index];
        const ComponentTypeID tid = TypeOf<T>();
        if (!(rec.mask & (1ULL << tid)))
            return nullptr;
        return &_archetypes.at(rec.mask).Columns.at(tid).Get<T>(rec.slot);
    }

    // Returns a query view over all archetypes matching the requested component set.
    template<typename... Ts>
    QueryView<Ts...> Query()
    {
        ComponentMask mask = 0;
        ((mask |= (1ULL << TypeOf<std::remove_const_t<Ts>>())), ...);
        return QueryView<Ts...>(_archetypes, mask);
    }

    void RegisterSystem(std::string_view name, UpdateFn fn);
    void RegisterRenderSystem(std::string_view name, RenderFn fn);

    void RunUpdateSystems(Scene& scene, f32 dt);
    void RunRenderSystems(Scene& scene, FramePacket& packet);
    
    Transform2D* GetTransform(Entity entity);

private:
    struct EntityRecord
    {
        ComponentMask mask       = 0;
        u32           slot       = 0;
        u32           generation = 0;
    };

    std::vector<EntityRecord>                    _entityTable;
    std::vector<u32>                             _freeList;
    std::unordered_map<ComponentMask, Archetype> _archetypes;

    std::vector<std::pair<std::string, UpdateFn>> _updateSystems;
    std::vector<std::pair<std::string, RenderFn>> _renderSystems;

    Archetype& GetOrCreateArchetype(ComponentMask mask);
    void       SwapRemoveFromArchetype(Archetype& arch, u32 slot);
};

// ---------------------------------------------------------------------------
// QueryView — iterates archetypes matching the requested component types.
// ---------------------------------------------------------------------------
template<typename... Ts>
class QueryView
{
    template<size_t I>
    using Elem = std::remove_const_t<std::tuple_element_t<I, std::tuple<Ts...>>>;

public:
    QueryView(std::unordered_map<ComponentMask, Archetype>& archetypes, const ComponentMask include) :
        _archetypes(archetypes), _includeMask(include)
    {}

    // Exclude archetypes containing U.
    template<typename U>
    QueryView& Without()
    {
        _excludeMask |= (1ULL << TypeOf<std::remove_const_t<U>>());
        return *this;
    }

    // Include only entities whose Tag.Value matches.
    QueryView& WithTag(const std::string_view tag)
    {
        _tag = std::string(tag);
        return *this;
    }

    // Invoke fn for each entity in matching archetypes.
    // fn may take (Entity, T0&, T1&...) or just (T0&, T1&...).
    template<typename Fn>
    void ForEach(Fn&& fn)
    {
        for (auto& [mask, arch] : _archetypes)
        {
            if ((mask & _includeMask) != _includeMask)
                continue;
            if (_excludeMask && (mask & _excludeMask))
                continue;

            const u32 count = static_cast<u32>(arch.Entities.size());
            for (u32 i = 0; i < count; ++i)
            {
                if (!_tag.empty() && !TagMatches(arch, i))
                    continue;
                Dispatch(fn, arch, i, std::index_sequence_for<Ts...>{});
            }
        }
    }

private:
    bool TagMatches(Archetype& arch, const u32 i) const
    {
        const ComponentTypeID tid = TypeOf<Tag>();
        const auto            it  = arch.Columns.find(tid);
        if (it == arch.Columns.end())
            return false;
        return it->second.Get<Tag>(i).Value == _tag;
    }

    template<size_t I>
    Elem<I>& GetElem(Archetype& arch, const u32 slot)
    { return arch.Columns.at(TypeOf<Elem<I>>()).template Get<Elem<I>>(slot); }

    template<typename Fn, size_t... Is>
    void Dispatch(Fn& fn, Archetype& arch, const u32 slot, std::index_sequence<Is...>)
    {
        if constexpr (std::is_invocable_v<Fn, Entity, Elem<Is>&...>)
            fn(arch.Entities[slot], GetElem<Is>(arch, slot)...);
        else
            fn(GetElem<Is>(arch, slot)...);
    }

    std::unordered_map<ComponentMask, Archetype>& _archetypes;
    ComponentMask                                 _includeMask = 0;
    ComponentMask                                 _excludeMask = 0;
    std::string                                   _tag;
};

} // namespace Arcbit
