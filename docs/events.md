# Events

A named publish / subscribe bus, so systems that don't know about each other
can react to what happens: a HUD counts coins it never sees being picked up,
UI swaps its button hints when the player picks up a gamepad, a quest moves on
when a scene loads. Lua scripts subscribe by name, and the editor's event
monitor (Phase 40) will show the live stream.

- [Bus or callback?](#bus-or-callback)
- [Names and ids](#names-and-ids)
- [Payloads](#payloads)
- [Publishing and delivery](#publishing-and-delivery)
- [Subscribing](#subscribing)
- [Engine events](#engine-events)
- [Entity events](#entity-events)
- [Debugging](#debugging)

---

## Bus or callback?

Both exist, for different jobs.

| Use | When | Example |
|-----|------|---------|
| **The bus** | something happened, and whoever cares - which the publisher doesn't know - should hear it | a coin was collected; a scene loaded; settings changed |
| **A component callback** | this entity, right now: targeted, ordered, cheap | `TriggerCallback`, `CollisionCallback`, `Animator::OnEvent` |

Bridging is one line - a trigger that wants the world to know publishes from
its callback:

```cpp
.OnEnter = [this](const Entity self, const Entity other) {
    GetApp().GetEvents().Publish(CoinCollected, { { "amount", 1 }, { "by", other } });
    GetWorld().DestroyEntity(self);
},
```

So physics contacts don't each go on the bus (one per contact, per frame,
would flood it); a game publishes the ones that mean something.

---

## Names and ids

Events are named with lower-case dot paths and hashed to an `EventId` - the
same FNV-1a hash input actions use (`HashName`), so C++ publishes and
subscribes with constants and no string work:

```cpp
constexpr EventId CoinCollected = MakeEvent("game.coin.collected");
```

Lua and the editor use the names. The bus learns a name when it's used by
name (`Subscribe("game.coin.collected", ...)`, `Publish("...")`) or registered
up front (`events.Register("game.coin.collected")`, e.g. in `OnStart`), and
logs an error if two names ever hash to the same id. `Describe(id)` gives the
name, or `#1a2b3c4d` for an id it never heard named.

**`engine.` is reserved.** The engine's own events start with it. Game code
can subscribe to them, but its `Publish` of one is dropped with a warning, so
they always mean what this page says; the engine raises them with
`PublishEngine`. Everything else is the game's: `game.coin.collected`,
`quest.started`, `boss.defeated`.

---

## Payloads

An event carries a few named fields - `EventPayload` - of a closed set of
types: `bool`, integer (`i64`), decimal (`f64`), text, `Entity` and `Vec2`.

```cpp
events.Publish(CoinCollected, { { "amount", 5 }, { "by", player } });

const i64    amount = e.Payload.Get<i64>("amount", 1); // fallback if missing
const Entity by     = e.Payload.Get<Entity>("by", Entity::Invalid());
```

- Integer literals land as `i64`, decimals as `f64`; `Get` converts between
  the number types (`Get<f32>`, `Get<i32>` of either work).
- A missing field, or one of another kind, returns the fallback.
- Not `std::any`, on purpose: Lua, the editor monitor and a save system must
  be able to read every payload. Keep it small and flat - an event says what
  happened; it doesn't carry game state.
- `ToString()` prints `amount=5 by=entity(3:1)` for logs.

---

## Publishing and delivery

```cpp
events.Publish(CoinCollected, payload);    // queued
events.PublishNow(CoinCollected, payload); // delivered before returning
```

`Publish` queues; the bus delivers the queue **once per fixed tick, after the
scene update and before the UI**. Handlers never run in the middle of the code
that published, so one can destroy entities or switch scenes without pulling
the world out from under it.

- Delivery is in publish order; each event's handlers run in subscription
  order.
- Events published while the queue is being delivered go out in the same
  pass. A pass delivers at most 4096 (`MaxPerFlush`) - a guard against two
  handlers publishing to each other forever; the rest wait for the next tick,
  with a warning.
- `PublishNow` is for the rare case that must be heard before the publisher
  carries on.
- Main thread only: not from `Scene::OnPrepare` (asserted in debug).

---

## Subscribing

```cpp
Subscription sub = events.Subscribe(CoinCollected, [this](const Event& e) {
    _coins += e.Payload.Get<i64>("amount", 1);
});
```

`Subscribe` returns an owned `Subscription`: destroying it (or `Reset()`)
unsubscribes. Keep it as a member of whatever the handler captures, so the
two go together. It's move-only, and outliving the bus is harmless.

- Unsubscribing during delivery is safe - a handler can even end its own
  subscription. A handler subscribed during delivery starts with the next
  event.
- **Scenes:** `Scene::Subscribe(id, handler)` holds the subscription in the
  scene, ending it right after `OnUnload` - no cleanup to write, and nothing
  reaches an unloaded scene. A scene suspended under a pushed one keeps
  listening: it's paused, not gone.
- `SubscribeAll(handler)` hears every event, before its own subscribers.

---

## Engine events

All in `EngineEvents` (`arcbit/events/EngineEvents.h`); every bus registers
their names.

| Event | Payload | When |
|-------|---------|------|
| `engine.scene.loaded` | `name`, `entry` | a scene finished `OnLoad` and is active |
| `engine.scene.unloaded` | `name` | a scene was unloaded |
| `engine.scene.suspended` | `name` | a scene was pushed over (it's paused) |
| `engine.scene.resumed` | `name` | the scene above was popped and this one is active again |
| `engine.input.device_changed` | `device`: `"keyboard_mouse"` / `"gamepad"` | the player used a different kind of device - swap button hints |
| `engine.item.added`, `engine.item.removed` | `bag`, `item`, `count` | a game bag gained / lost items ([items.md](items.md#the-games-bags)) |
| `engine.item.equipped`, `engine.item.unequipped` | `member`, `item`, `slot` | a party member put an item on / took it off ([items.md](items.md#equipment)) |
| `engine.item.used` | `item`, `member`, `bag` | an item was used ([items.md](items.md#using-items)) |
| `engine.party.joined`, `engine.party.left` | `member` | a character joined / left the party |
| `engine.currency.changed` | `currency`, `amount` (now), `change` | a currency changed through the game's currencies ([items.md](items.md#currencies)) |
| `engine.input.bindings_updated` | `count`, `text` (the notice's lines) | an update placed new or reset actions among the player's bindings, once the boot is done ([input.md](input.md#across-game-updates)) |
| `engine.window.focus_changed` | `focused` | the window gained or lost focus (pause on alt-tab) |
| `engine.window.resized` | `size` (`Vec2`) | the window's size changed |
| `engine.settings.changed` | `section`: `"audio"` / `"graphics"` / `"input"` / `"game"`; `key` for `"game"` | the player changed a setting (`Settings::Set` reports `"game"` with its key) |
| `engine.entity.spawned` | `entity`, `name`, `tag` | an entity with `EntityEvents` appeared |
| `engine.entity.destroyed` | `entity`, `name`, `tag` | an entity with `EntityEvents` was destroyed |
| `engine.entity.interacted` | `entity` (the one used), `by` | an `Interactor` used an `Interactable` ([scripting.md](scripting.md)) |
| `engine.game.saved` | `slot` (`"manual_3"`, `"auto_1"`, `"quick"`), `name` | a save was written ([saves.md](saves.md)) |
| `engine.game.loaded` | `slot` | a save's state is back, as its scenes load |
| `engine.locale.changed` | `from`, `to` | the player-visible language changed (`Application::SetLanguage`) |
| `engine.dialog.started` | `id`, `with` (the entity talked to, if any) | a conversation began ([dialog.md](dialog.md)) |
| `engine.dialog.line` | `id`, `key` (the line's Loc key), `speaker` (an id; `""` for narration) | a line showed |
| `engine.dialog.choice` | `id`, `choice` (its id) | the player picked a choice |
| `engine.dialog.ended` | `id`, `with`, `choice` (the last choice's id, or `""`) | the conversation ended |

Scene events are delivered at the flush after the swap; Application's default
unnamed scene isn't announced. The last-used device counts a key, mouse
button, scroll or real mouse movement as keyboard / mouse, and a gamepad
button or a stick or trigger pushed past half way as gamepad
(`InputManager::GetLastDevice`).

Lua subscribes and publishes the same events by name (`Event.Subscribe`,
`Event.Publish` - [scripting.md](scripting.md)).

Events for systems that don't exist yet - items, combat, weather,
achievements - arrive with those systems' phases.

---

## Entity events

Entities announce nothing by default. Give `EntityEvents` to the ones scripts
care about - the player, a boss, a door:

```cpp
world.AddComponent<EntityEvents>(player); // engine.entity.spawned / destroyed
world.AddComponent<EntityEvents>(boss, EntityEvents{ .Spawned = false, .DestroyedEvent = "boss.defeated" });
```

```json
"boss": { "EntityEvents": { "spawned": false, "destroyed_event": "boss.defeated" } }
```

- **Spawned** goes out the tick after the component is added - normally at
  creation - so the payload sees the entity's `Name` (a scene file adds it
  after `EntityEvents`).
- **Destroyed** goes out when the entity is destroyed. Removing the component
  isn't a death, and neither is the scene unloading - a scene ending isn't
  its entities dying.
- **Renaming** either event per entity (`"boss.defeated"`) means a listener
  hears exactly the entity it wants, instead of filtering every destroyed
  event. A renamed event is the game's, published as a normal event.
- Later systems add their per-entity events to the same component - combat's
  death (Phases 31 / 32), AI's "sees the player" (Phase 30) - so "on death" for
  one enemy is a field on that enemy.

---

## Debugging

- Every delivered event goes to the log on the `EventBus` channel (debug
  level): `engine.scene.loaded name="World" entry=""`.
- In the demo app, **F3** toggles an overlay of the latest 16 events, from
  startup - engine events in blue-gray, game events in gold. It doesn't take
  input, so menus under it still work.
- The Triggers demo publishes `game.coin.collected` from five coin triggers;
  a HUD counts them through `Scene::Subscribe`, and the last one publishes
  `game.coin.all_collected`.
- `SubscribeAll` is how a tool listens to everything - what the editor's
  event monitor (Phase 40) will pipe out.
