// Application: loot - containers opened (their loot rolled the first time),
// LootDrop's scattered pickups, and the console's loot (docs/items.md).

#include <arcbit/app/Application.h>
#include <arcbit/core/Log.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Pickup.h>

#include <cmath>
#include <format>
#include <map>
#include <stdexcept>

namespace Arcbit
{
void Application::OpenContainer(const Entity container, const Entity by)
{
    Scene*     scene = _scenes.GetActive();
    Container* c     = scene ? scene->GetWorld().GetComponent<Container>(container) : nullptr;
    if (!c) return;
    if (!c->Loot.empty()) FillFromLoot(*c);

    const std::optional<ContainerOpening> own = ParseContainerOpening(c->Open);
    if (!c->Open.empty() && !own) LOG_WARN(Engine, "Container: unknown \"open\" '{}' - the project's is used", c->Open);
    const ContainerOpening mode = own.value_or(_config.Defaults.ContainerOpen);
    EventPayload payload = { { "entity", container }, { "by", by }, { "mode", std::string(ContainerOpeningName(mode)) } };
    if (mode == ContainerOpening::TakeAll) {
        u32 taken = 0, left = 0;
        TakeAll(*c, taken, left);
        payload.Set("taken", taken);
        payload.Set("left", left);
    }
    _events.PublishEngine(EngineEvents::ContainerOpened, std::move(payload));
}

// The loot table rolled into the container, once: the table is cleared, and
// that's saved with what it rolled.
void Application::FillFromLoot(Container& container)
{
    for (const LootStack& stack : RollLoot(container.Loot)) {
        if (stack.Currency) container.Currency[stack.Id] += stack.Count;
        else if (container.Items.Add(stack.Id, stack.Count) < stack.Count)
            LOG_WARN(Engine, "Container: loot '{}' rolled more {} than it holds", container.Loot, stack.Id);
    }
    container.Loot.clear();
}

// Everything that fits into the party's bag; currencies all go.
void Application::TakeAll(Container& container, u32& taken, u32& left)
{
    Inventory&                     bag = _inventories->GetParty();
    const std::vector<ItemStack>   stacks = container.Items.GetStacks(); // a copy: removing changes them
    for (const ItemStack& stack : stacks) {
        const u32 moved = bag.Add(stack.Item, stack.Count);
        (void)container.Items.Remove(stack.Item, moved);
        taken += moved;
        left += stack.Count - moved;
    }
    for (const auto& [id, amount] : container.Currency) (void)_currencies->Add(id, amount);
    container.Currency.clear();
}

// Each stack its own pickup: the first where the entity was, the rest
// around it - far enough out that half-tile icons don't overlap.
void Application::DropLoot(Scene& scene, const std::string_view table, const Vec2 at)
{
    const std::vector<LootStack> stacks = RollLoot(table);
    const f32                    radius = scene.GetConfig().TileSize * 0.6f;
    for (size_t i = 0; i < stacks.size(); ++i) {
        const f32  angle = static_cast<f32>(i) * 2.39996f; // the golden angle: no two land together
        const Vec2 spot  = i == 0 ? at : at + Vec2{ std::cos(angle), std::sin(angle) } * radius;
        const LootStack& stack = stacks[i];
        if (!SpawnPickup(scene, stack.Currency ? nullptr : _items.Find(stack.Id), stack.Id, stack.Count, spot).IsValid())
            LOG_WARN(Engine, "Loot '{}': couldn't drop {} x {}", table, stack.Count, stack.Id);
    }
}

// loot lists the tables, or rolls one (many times: a tally, to check the odds).
void Application::RegisterLootCommands()
{
    using Args = DebugConsole::Args;
    _debugConsole.Register({ .Name = "loot", .Help = "loot [table] [times] - list the loot tables, or roll one (and tally)",
                             .Run = [this](const Args& a) {
                                 if (a.empty()) {
                                     for (const std::string& id : _loot.GetIds()) _debugConsole.Print("  " + id);
                                     return;
                                 }
                                 if (!_loot.Find(a[0])) throw std::runtime_error(std::format("no table '{}'", a[0]));
                                 const i64 times = a.size() > 1 ? DebugConsole::Integer(a[1], "times", 1, 100000) : 1;
                                 std::map<std::string, u64> total;
                                 for (i64 i = 0; i < times; ++i)
                                     for (const LootStack& stack : RollLoot(a[0])) total[stack.Id] += stack.Count;
                                 if (total.empty()) _debugConsole.Print("  nothing");
                                 for (const auto& [id, count] : total)
                                     _debugConsole.Print(times == 1 ? std::format("  {} x{}", id, count)
                                                                    : std::format("  {} x{} ({:.2f} a roll)", id, count, static_cast<f64>(count) / times));
                             } });
}
} // namespace Arcbit
