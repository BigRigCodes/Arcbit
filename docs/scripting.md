# Scripting

Game logic in Lua 5.4: a chest that opens, a door to another scene, a guard
on patrol, a scene reacting to the player, a menu's buttons. Scripting is
opt-in - an entity gets a `Script` component, a scene or UI screen names a
script - and everything else runs no Lua at all. Scripts reach the engine
through a sandboxed API and can't crash or hang the game.

The **Scripted** demo is built this way end to end: `game/assets/scenes/scripted.arcscene`
and `game/assets/scripts/demo/`.

- [A first script](#a-first-script)
- [Where scripts live](#where-scripts-live)
- [Entity scripts](#entity-scripts)
- [Scene scripts](#scene-scripts)
- [Hooks](#hooks)
- [The engine API](#the-engine-api)
- [Tasks and timers](#tasks-and-timers)
- [Interaction](#interaction)
- [Saving script state](#saving-script-state)
- [Lua UI](#lua-ui)
- [The console](#the-console)
- [Hot reload and errors](#hot-reload-and-errors)
- [The sandbox and its limits](#the-sandbox-and-its-limits)
- [From C++](#from-c)

---

## A first script

`assets/scripts/demo/chest.lua`:

```lua
local Chest = { Properties = { Gold = 10 } }   -- defaults; a scene file overrides them

function Chest:OnInteract(by)
    if self.Saved.Opened then return end       -- Saved goes into save files
    self.Saved.Opened = true
    Save.Set("gold", Save.Get("gold", 0) + self.Properties.Gold)
    Event.Publish("game.toast", { text = "Found " .. self.Properties.Gold .. " gold!" })
end

return Chest
```

and an entity in a scene file:

```json
"chest": {
    "Interactable": { "prompt": "Open" },
    "Persistent": {},
    "Script": { "script": "demo.chest", "properties": { "Gold": 25 } },
    "Transform2D": { "position": [384, -192] }
}
```

---

## Where scripts live

Scripts are modules under `assets/scripts/`, named like `require` names them:
`demo.chest` is `assets/scripts/demo/chest.lua`. A module returns a table of
hooks and anything else it wants to share; `require("demo.util")` loads another
module (once - every `require` of it shares the same table).

Names use PascalCase like the rest of the engine: `Entity.Find`,
`self.Properties`, `OnStart`.

---

## Entity scripts

The `Script` component gives one entity a script:

| Field | Scene file | |
|---|---|---|
| `Module` | `script` | the module name |
| `Properties` | `properties` | overrides of the module's `Properties` |
| `Saved` | `saved` | `self.Saved` as of the last save (written by the engine) |

Each scripted entity gets its own instance, `self`:

| | |
|---|---|
| `self.Entity` | its entity (a handle - see [Entity](#entity)) |
| `self.Properties` | the module's `Properties` (copied), the component's on top |
| `self.Saved` | state that goes into saves - see [Saving script state](#saving-script-state) |
| `self:Run(fn, ...)` | start a [task](#tasks-and-timers) |

Functions and data on the module are shared by every instance (through the
instance's metatable); anything set on `self` is that entity's own.

---

## Scene scripts

A scene can have a script too: `"script": "demo.scene"` at the top of an
`.arcscene`, or `SetScript("demo.scene")` in a C++ scene's constructor or
`OnLoad`. Its hooks are `OnLoad`, `OnUpdate(dt)` and `OnUnload`, with `self` the
same kind of instance (no entity).

---

## Hooks

Each is optional - a module defines the ones it needs.

| Hook | When |
|---|---|
| `OnLoad()` | scene scripts: the scene has loaded |
| `OnStart()` | the scene has loaded *and its saved state is applied*, so the entity is as the player left it; or the entity was scripted later (spawned) |
| `OnUpdate(dt)` | every fixed tick, after the scene's own `OnUpdate`, before its systems |
| `OnDestroy()` | the entity is destroyed, its `Script` removed, or its scene unloads - its components can still be read |
| `OnUnload()` | scene scripts: the scene is unloading (after every `OnDestroy`) |
| `OnInteract(by)` | an Interactor used this entity ([Interaction](#interaction)) |
| `OnTriggerEnter(other)` / `OnTriggerStay(other)` / `OnTriggerExit(other)` | trigger overlaps with this entity |
| `OnCollisionEnter(other, normal)` / `OnCollisionExit(other)` | blocking contacts |
| `OnEvent(name, payload)` | events subscribed with `Event.Subscribe(name)` and no function |

Trigger and collision hooks run alongside the entity's C++ `TriggerCallback` /
`CollisionCallback`: both fire.

A suspended scene's scripts don't update (or run tasks) until it's resumed.

---

## The engine API

### Entity

A script holds entities as handles - an entity plus the scene it's in. A
handle to an entity that's gone (destroyed, or its scene unloaded) is stale:
`IsValid()` is false, reads give `nil`, writes are ignored with a warning.

| | |
|---|---|
| `Entity.Find(id)` | a scene file's entity id, else a `Persistent` id, else a `Name`; `nil` if none |
| `Entity.FindAll(tag)` | every entity with the tag, as a list |
| `Entity.With("Chest", "Collider2D")` | every entity with all the named components |
| `Entity.Create({ Transform2D = { position = { 0, 0 } }, Tag = { value = "Drop" } })` | a new entity from component tables |
| `e:IsValid()`, `e:Destroy()` | |
| `e:Get(name)` | the component as a table (its scene-file form), or `nil` |
| `e:Set(name, fields)` | change only the given fields |
| `e:Add(name, fields)`, `e:Remove(name)`, `e:Has(name)` | |
| `e.Position` | a `Vec2`, read and write (the collider follows) |
| `e.Tag` (read / write), `e.Name` (read) | |

Components work by name through the scene registry, so a game's own
registered components work in Lua with no extra binding. Their fields use the
scene-file names (`position`, `tint`, `value`).

In `OnDestroy` a script can still read its entity and create others (loot,
splinters), but can't destroy or reshape the dying entity. Creating,
destroying, adding or removing inside a system's loop is refused with an
error rather than corrupting the world.

### Scene, Tilemap, Input, Audio

| | |
|---|---|
| `Scene.GetName()`, `Scene.GetEntry()` | the scene the script belongs to |
| `Scene.Switch(name, entry)`, `Scene.Push(name, entry)`, `Scene.Pop()` | |
| `Tilemap.GetTile(x, y, layer)`, `Tilemap.SetTile(x, y, layer, id)`, `Tilemap.IsSolid(x, y)` | layer defaults to 0 for GetTile |
| `Tilemap.WorldToTile(pos)` -> `x, y`, `Tilemap.TileToWorld(x, y)` -> `Vec2` | |
| `Input.IsDown(action)`, `Input.IsPressed(action)`, `Input.IsReleased(action)` | by action name; false while a menu is open or the screen fades |
| `Audio.PlaySound(path, volume)`, `Audio.PlayMusic(path, loop)`, `Audio.StopMusic()` | music follows the scene, as from C++ |

### Save and Event

| | |
|---|---|
| `Save.Get(key, fallback)`, `Save.Set(key, value)`, `Save.Has(key)`, `Save.Erase(key)` | the game's `SaveData`: booleans, integers, decimals, text, `Vec2`; `Set(key, nil)` erases |
| `Save.To(3)`, `Save.To("quick")`, `Save.Load(...)`, `Save.Auto()` | saving and loading ([saves.md](saves.md)) |
| `Event.Subscribe(name, fn)` | `fn(payload, name)`; without `fn`, the script's `OnEvent`. Returns a handle with `:Unsubscribe()` |
| `Event.Publish(name, { field = value })` | fields: booleans, numbers, text, `Vec2`, entities. `engine.` events can't be published |

Subscriptions end with the script that made them.

### Loc

Player-visible text belongs in the locale files as keys, so it's shown in the
player's language (docs/localization.md).

| | |
|---|---|
| `Loc.Get(key)` | the string in the current language (else English, else the key itself) |
| `Loc.Format(key, { name = "Ana", count = 3 })` | with `{name}` placeholders filled; a number named `count` picks the plural form |
| `Loc.Interpolate(text, args)`, `Loc.Has(key)` | placeholders in text that isn't a key; whether a key has a string |
| `Loc.FormatNumber(n, decimals)`, `Loc.FormatDate(os.time(), "datetime")` | a number or a time in the language's style |
| `Loc.Language()`, `Loc.Languages()` | the current code (`"fr"`); every language as `{ Code, Name }` |
| `Loc.SetLanguage(code)` | switch, as the Game settings screen does (saved, `engine.locale.changed` fires); false if there's no such language |

A widget's `.TextKey = "ui.shop.title"` shows that key and follows language
changes; setting `.Text` shows exactly that text instead.

### Basics

`print(...)` and `Log.Info / Warn / Error(...)` (prefixed with the script and
line) go to the log and the console. `Vec2(x, y)` has `.X`, `.Y`, `+ - * /`,
`==`, `Length`, `LengthSquared`, `Normalized`, `Dot`, `Distance`.

---

## Tasks and timers

Lua coroutines, for things that take time - a patrol, a door that closes
again, a short scripted beat - without a state machine:

```lua
function Guard:OnStart()
    self:Run(function()
        while true do
            self.Entity.Position = Vec2(0, 0)   Wait(2)
            self.Entity.Position = Vec2(64, 0)  Wait(2)
        end
    end)
    Timer.After(5, function() Log.Info("shift change") end)
end
```

| | |
|---|---|
| `self:Run(fn, ...)` / `Task.Run(fn, ...)` | start a task; it runs at once up to its first `Wait` |
| `Wait(seconds)`, `Wait()` | pause it (no seconds: until the next tick) - only inside a task |
| `WaitUntil(test)` | pause until `test()` is true, checked each tick |
| `Timer.After(seconds, fn)`, `Timer.Every(seconds, fn)` | |
| `task:Stop()`, `task:IsRunning()` | |

A task advances with its scene's update and ends with the script that started
it (or its scene). One that ends its own script mid-run stops at its next
`Wait`. An error in a task is logged and ends only that task.

---

## Interaction

Give the player an `Interactor` and the things it can use an `Interactable`.
Pressing the Interactor's action picks one target and calls its script's
`OnInteract(by)`, and publishes `engine.entity.interacted` (`entity`, `by`)
for C++ to react to as well.

| Component | Fields (scene file) |
|---|---|
| `Interactor` | `action` (default `"Interact"`), `reach` (pixels; 0 = 1.25 tiles) |
| `Interactable` | `prompt` ("Open", "Talk" - for a prompt UI) |

Which target: a **tile mover** uses exactly the tile it faces; a **free mover**
uses the nearest interactable in reach within 60 degrees of where it faces,
else the nearest in reach. Facing comes from `MoveIntent`. A `Disabled` one is
skipped. Nothing happens while a menu is open.

---

## Saving script state

`self.Saved` is copied into the entity's `Script` component just before its
scene's state is captured, so on a `Persistent` entity it saves and loads with
the game ([saves.md](saves.md)) - an opened chest stays open. It's restored
before `OnStart`. Plain values only: booleans, numbers, text and tables of
them (a `Vec2` comes back as `{ x, y }`). Anything else in `self` lasts only
while the scene is loaded.

---

## Lua UI

An `.arcui` layout can name a script and its widgets' handlers:

```json
{ "script": "demo.menu",
  "widgets": [
    { "type": "Label",  "name": "gold" },
    { "type": "Button", "name": "buy", "text": "Buy", "on_click": "OnBuy" },
    { "type": "Slider", "name": "volume", "on_change": "OnVolume" } ],
  "meta": { "layer": "hud" } }
```

`UI.Open(path)` opens it (from C++: push a `ScriptedScreen`). The script is a
module of hooks - `OnEnter`, `OnExit`, `OnUpdate(dt)`, `OnBack` (default:
close) - and the handlers: `self:OnBuy(widget)` for `on_click` (buttons),
`self:OnVolume(widget, value)` for `on_change` (sliders, switches,
checkboxes, text inputs, dropdowns and radio groups - choices count from 1).

`self.Screen:Find(name)` gives a widget with `.Text`, `.TextKey`, `.Value`, `.Visible`,
`.Enabled`, `.Name`; `self.Screen:Close()`, `:IsOpen()`. `"meta": { "layer":
"hud" }` makes it a HUD (no input) instead of a menu. A UI script has the
whole API (acting on the active scene); its tasks and subscriptions end when
it closes. The engine's own screens (pause menu, settings) keep their C++
logic.

---

## The console

`` ` `` opens it (rebindable: `Engine_Console`). Commands, or Lua for anything
that isn't one - expressions print their value. Enter runs a line, Up / Down
walk the history, and as you type the likely rest of a command or Lua name
shows dimmed: Tab takes it (`he` -> `help`, `Entity.Fi` -> `Entity.Find`,
`pos:Len` -> `pos:Length`). Back closes it.

| Command | |
|---|---|
| `help [command]` | list commands, or explain one |
| `clear` | |
| `scene <name> [entry]` | switch scenes |
| `tp <x> <y>` | move the player (the first Interactor, else tag `Player`) - cheat |
| `timescale <n>` | game speed; 0 pauses - cheat |
| `save [n \| quick]`, `load [n \| quick]` | |
| `set <key> <value>` - cheat, `get <key>` | save data |
| `fps`, `colliders` | toggles |
| `lang [code]` | list the languages (current marked), or switch to one |
| `loc.missing [code]` | the English keys a language has no string for |
| `screenshot` | save the screen (closing the console first) as a PNG in the user folder's `screenshots/` - also `Application::Screenshot()` |
| `quit` | |

Games add their own - from C++ `GetConsole().Register({ .Name, .Help, .Cheat,
.Run })`, from Lua `Console.Register("heal", fn, "heal <n> - restore health",
true)` (ending with the script that registered it). `Console.Print(...)`
writes to it.

Who may open it: `_consoleConfig.Access` - `Off`, `DevOnly` (default: debug
builds), `Players` (shipped builds too). A cheat command, or any Lua typed in,
sets `engine.cheats_used` in the save data, so a game can tell (say, to turn
off achievements). `_consoleConfig.FontScale` and `Height` size it.

While anything is being typed into (the console, any text field), the game's
own keys rest: only the UI's keys and a few engine ones work.

---

## Hot reload and errors

**Hot reload** (debug builds): save a script while the game runs and it
reloads within a second. Running instances keep their state and the module's
existing data, and pick up the new functions and the new `Properties`
defaults (re-read for every instance, scene-file overrides still on top); an
`OnUpdate` an error had turned off runs again. A save that doesn't load is
reported and the old code keeps running.

The build copies assets into `bin/`, so the running game has to load scripts
from the files you edit: `ApplicationConfig::ScriptRoot` says where. The demo
app points it at `game/assets/scripts` in debug builds (the
`ARCBIT_GAME_ASSETS` define in `game/CMakeLists.txt`); a shipped build uses
`assets/scripts`.

Locale files hot-reload the same way: on screen text follows within a second.
`ApplicationConfig::LocaleFolder` is the game's folder (the demo uses
`game/assets/locale` in debug builds); the engine's own strings reload from
`bin/`, after a build copies them there.

**Errors** never take the game down. Every call into Lua is protected; an
error is logged with the script, line and traceback, printed in the console,
and in debug builds shown in the top-right corner for a few seconds. A hook
that fails is abandoned for that call; an `OnUpdate` that fails is turned off
for that script until it's reloaded.

---

## The sandbox and its limits

Scripts get base Lua (no `load`, `loadfile`, `dofile`, `collectgarbage`),
`string`, `table`, `math`, `utf8`, `coroutine` and `os.time / clock / date /
difftime` - no `io`, `os.execute`, `debug` or `package`, and no bytecode.
`require` loads only from the script root. Each module has its own
environment and its own copies of the libraries, so scripts can't tamper with
each other.

Limits (`ScriptLimits`): a call from the engine into Lua runs at most 50
million instructions (a frozen loop becomes an error, even through `pcall`),
and the Lua state at most 256 MB.

---

## From C++

- `Application::GetScripts()` - the `ScriptEngine`: `Run(code)`,
  `Require(module)`, `Complete(line)`, `CheckReloads()`.
- `Scene::SetScript(module)`; the `Script`, `Interactor` and `Interactable`
  components; `ScriptedScreen` for Lua UI; `Application::GetConsole()`.
- sol2 never appears in engine headers - the binding lives in
  `engine/src/Script*.cpp` (`ScriptState.h`), so builds stay fast.
