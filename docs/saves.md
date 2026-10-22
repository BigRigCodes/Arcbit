# Saves

Save the player's game to a slot and load it back as they left it: where they
are, what they changed in the world, and the game's own state. Separate from
settings, which are the player's preferences, not their game.

- [Quick start](#quick-start)
- [What a save holds](#what-a-save-holds)
- [Saving changes, not the world](#saving-changes-not-the-world)
- [Game data and participants](#game-data-and-participants)
- [Saving and loading](#saving-and-loading)
- [Slots and files](#slots-and-files)
- [Sealed saves](#sealed-saves)
- [Migrations](#migrations)
- [Autosave, the indicator and the notice](#autosave-the-indicator-and-the-notice)
- [The save / load screens](#the-save--load-screens)
- [Thumbnails](#thumbnails)
- [Events](#events)
- [Debugging](#debugging)

---

## Quick start

```cpp
// OnStart: the game's own seal key, and what should persist.
GetSaves().SetSealKey("my-studio/my-game/saves");
world.AddComponent<Persistent>(player, Persistent{ .Id = "player", .Components = { "Transform2D" } });
GetSaves().Data().Set("quest.dragon", std::string("started"));

// A pause menu's buttons (or your own UI):
SaveTo(SaveSlot::Manual(1));   // or ShowSaveScreen() for the slot list
LoadFrom(SaveSlot::Manual(1)); // or ShowLoadScreen()
AutoSave();                    // the next of the rotating autosave slots
```

In a scene file, an entity opts in with `"Persistent": {}` (its file id is
its save id), or the whole scene does with `"persist_all": true`.

---

## What a save holds

One JSON document - sealed in release builds, plain while developing:

```jsonc
{
    "format": 1,        // the engine's save format
    "game_version": 3,  // the game's own save version (migrations)
    "meta":     { "name": "Village", "scene": "Village", "saved_at": 1760000000, "playtime": 4521.5 },
    "data":     { "gold": 120, "quest.dragon": "started" },
    "sections": {
        "world":     { "scenes": { "Village": { ... } } }, // every visited scene's changes
        "scenes":    { "stack": [ { "name": "Village", "entry": "north_gate" } ] },
        "inventories": { "party": { ... } },               // the game's bags (docs/items.md)
        "party": { "members": [ ... ] },                   // who's in the party, their stats and equipment
        "quests": { ... }                                  // a game participant
    }
}
```

`world`, `scenes`, `inventories` and `party` are the engine's own sections;
don't register participants under those names.

---

## Saving changes, not the world

A loaded scene is always rebuilt from its source - its scene file or C++
`OnLoad` - and the player's changes are applied on top. So a content update (a
new NPC, a fixed collider, a moved chest) still reaches old saves, and saves
stay small.

**Only `Persistent` entities are tracked.** Everything else - scenery,
particles, projectiles - comes back as authored.

```cpp
struct Persistent
{
    std::string              Id;           // stable across loads; a scene file's entity id by default
    bool                     Enabled = true;
    std::vector<std::string> Components;   // registered component names to keep; empty = all of them
};
```

The id is what matches a saved entity to the rebuilt one, so it has to be the
same every time the scene builds. C++ scenes set one; scene files use the
entity's id. Ids only need to be unique within a scene.

What a scene's state records:

| | Saved as | Applied |
|---|---|---|
| A field changed | only that field, compared with how it was authored | written back through the registry |
| A component added at runtime | all its fields | added |
| A component removed | `null` | removed |
| An authored entity destroyed (the beaten boss) | its id | destroyed again |
| A persistent entity spawned at runtime (a dropped item) | every component | rebuilt |
| A tile set at runtime (a bridge you built) | `[x, y, layer, id]` | set again |

Comparing with *authored*, not with defaults, matters both ways: a chest
authored open that the player closed comes back closed, and a field the
player never touched isn't stored, so a content update to it shows.

**Scene-wide.** `"persist_all": true` in an `.arcscene` makes every entity
persistent under its file id; one opts back out with
`"Persistent": { "enabled": false }`. Saving the scene file doesn't write the
implied `Persistent` components back out.

**When it happens** (`SceneManager`, with `Application`'s host):

```
OnLoad          the scene builds itself as authored
Snapshot        every persistent entity's components, as authored
Apply           the saved changes, on top (tile changes tracked from here)
... play ...
Capture         what changed since the snapshot - when the scene unloads,
                and for the scenes still loaded whenever a save is made
```

So a scene remembers its changes after it's gone - leave the village and
come back, and the chest is still open - and a save holds every scene the
player has visited, not just the current one.

Only named scenes (registered with `SceneManager`) are tracked, and only the
components the `SceneRegistry` knows how to read and write. Colliders follow
restored transforms into the physics broadphase.

---

## Game data and participants

**`SaveData`** (`GetSaves().Data()`) is a key / value store for flags and
counters - string keys, the same value types as event payloads (`bool`, `i64`,
`f64`, `std::string`, `Vec2`) bar entities:

```cpp
SaveData& data = GetSaves().Data();
data.Set("gold", static_cast<i64>(120));
const i64 gold = data.Get<i64>("gold", 0); // numbers convert on read
```

Scripts read and write the same store (`Save.Get` / `Save.Set`,
[scripting.md](scripting.md)), and dialog conditions and actions test and
change it ([dialog.md](dialog.md)).

**Participants** are systems with structured state of their own (a quest
log; the engine's bags are one too). Each registers write / read callbacks
and gets its own section:

```cpp
GetSaves().RegisterParticipant(
    "quests",
    [this](nlohmann::json& section) { section = _quests.ToJson(); },
    [this](const nlohmann::json& section) { _quests.FromJson(section); });
```

A participant added after a save was made reads an empty object from it.

---

## Saving and loading

`Application` owns the save system (`GetSaves()`) and the world state:

| Call | What it does |
|---|---|
| `SaveTo(slot, name = {})` | capture the live scenes, write the save, show the indicator, take a thumbnail. `name` labels it in the slot list (empty = the active scene's name). |
| `LoadFrom(slot)` | read the save, then at the next scene swap: the current scenes unload, the save's data and world state go back, its scene stack loads (with entry points), playtime is restored |
| `AutoSave()` | `SaveTo(GetSaves().NextAutoSlot())` |
| `ResetGame()` | forget the current game: data, participants, world state, playtime. The title's New Game does this. |
| `GetPlaytime()` | seconds of play, counted only while the player has control (not under a menu, a fade or the splash) |

Both refuse (return `false`, logged) while a scene change is under way - the
scenes are mid-swap - and `SaveTo` also with no named scene loaded.

Loading replaces the whole scene stack in one fade (`SceneManager::Restore`):
the save's state goes in *after* the old scenes unload, so they can't write
over it, and scenes under the top load already suspended, as they were.

The title menu shows **Continue** (the newest save) and **Load Game** once
there's a save.

---

## Slots and files

Saves live in the player's own data folder, with their settings, never next
to the executable:

```
%APPDATA%\<Studio>\<Game>\          ApplicationConfig::Studio / GameName (UserDataFolder)
    settings.json
    saves\
        slot_01.arcsave  slot_01.png   manual slots (10)
        auto_1.arcsave   auto_1.png    rotating autosaves (3)
        quick.arcsave    quick.png     the quicksave
```

The counts are `SaveManager::ManualSlots` / `AutoSlots`. `NextAutoSlot()` is
the first empty autosave, else the oldest.

Writes are crash-safe: the new save goes to a `.tmp` file, the slot's current
save becomes its `.bak`, then the temp file takes its place. A slot that's
missing or won't parse falls back to its `.bak`. A save that parses but is
refused - from a newer game, or missing a migration - does *not* fall back:
an older backup loading instead would silently lose progress.

`List()` gives every slot with a save, newest first, with its metadata and
thumbnail path. `Delete(slot)` removes the save, its backup and thumbnail.

---

## Sealed saves

A plain JSON save is a free cheat - open it, set gold to a million. Release
builds seal every save and refuse plain ones:

- The JSON is compressed (zstd) and encrypted with XChaCha20-Poly1305
  (libsodium). The file is an `ARCSAVE` header, a random nonce and the
  ciphertext; the header is authenticated too.
- Changing even one byte fails the check: the save is refused (logged) and
  the slot's backup loads instead.
- Saves aren't tied to the machine, so players can copy them and Steam Cloud
  (Phase 39) can sync them.

The key comes from a secret only the game knows:

```cpp
GetSaves().SetSealKey("my-studio/my-game/saves");
```

Without one, a shared engine default is used (logged once). Phase 36's
project keys replace this: the dev key normally, or the mod key for games
with `mod_support = true`, so the editor's mod mode (Phase 40) can open, edit
and reseal saves.

No client-side scheme stops a determined reverse engineer - the game has to
read its own saves, so the key ships with it. Sealing makes editing
impractical for everyone else.

**Debug builds** write plain JSON and read both, so saves stay readable while
developing (`SealSaves` / `AcceptPlainSaves` change either).

---

## Migrations

Every save records the game's save version. When the shape of what you save
changes, bump `CurrentGameVersion` and register a step that upgrades the
previous version's document:

```cpp
GetSaves().CurrentGameVersion = 2;
GetSaves().RegisterMigration(1, [](nlohmann::json& save) {
    // v1 kept gold as a string
    auto& data = save["data"];
    if (data.contains("gold")) data["gold"] = std::stoll(data["gold"].get<std::string>());
});
```

Reading an older save applies the steps in order. A save from a newer game,
or one with a step missing, isn't loaded (logged).

---

## Autosave, the indicator and the notice

Set `_saving` (`SavingConfig`) in the constructor or `OnStart`:

| Field | Default | |
|---|---|---|
| `IndicatorIcon` | `assets/engine/ui/saving.png` | spins in the bottom-right corner, with "Saving...", on every save; empty = text only |
| `IndicatorTint` | white | the icon's color (the default icon is white) |
| `IndicatorSeconds` | 1.5 | how long it shows per save, fades included |
| `Notice` | `SaveNotice::FirstLaunch` | the saving notice: `Never`, `FirstLaunch` or `EveryLaunch` |
| `NoticeSeconds` | 5 | how long the notice card shows |
| `AutoSaveOnSceneChange` | false | autosave each time a scene change settles |

**The notice** is a splash card after the logos: the icon spinning above
white text on black, telling the player not to quit or power off while it
shows. On `FirstLaunch` it counts as seen once it starts playing (kept in
their settings), so skipping the splash before it means it shows next time.
The text is the `ui.save_notice.text` locale string.

**Autosave on scene changes** waits until the change has finished and no
menu is open, so a scene loaded under the title autosaves once the game
starts; a change that loaded a save doesn't autosave. Call `AutoSave()`
yourself for checkpoints, resting and the like.

The editor (Phase 40) will set these per project.

---

## The save / load screens

`ShowSaveScreen()` and `ShowLoadScreen()` open `SaveSlotScreen`:

- **Save** lists every manual slot, empty or not; picking a used one asks
  "Overwrite this save?" (`Confirm`, a `ConfirmScreen` with No focused first).
- **Load** lists every save - manual, auto and quick - newest first; loading
  closes every menu.

Each row shows the thumbnail, the slot and name, then the scene, playtime and
date. The frame is `assets/engine/ui/save_slots.arcui`; the rows are built in
code. `PauseMenuScreen` has Save Game / Load Game buttons, off unless a game
turns them on (`ShowSaveGame`, `ShowLoadGame`) and wires them to these.

---

## Thumbnails

Every save takes a picture of the frame it was made on - the world only, with
no menus, HUD, fades or saving icon, so saving from a pause menu still gets a
clean shot. The render thread splits that one frame: it draws the world,
copies the image back (`FramePacket::CaptureWorld`,
`RenderDevice::CopySwapchainImage`), then draws the UI on top.
`SaveManager::WriteThumbnail` scales it to `ThumbnailWidth` (320 px) and
writes a PNG beside the save.

Thumbnails aren't sealed - a picture gives nothing away. The readback stalls
the GPU once per save. Saves made before thumbnails show an empty frame.

---

## Events

| Event | Payload | When |
|---|---|---|
| `engine.game.saved` | `slot` (`"manual_3"`, `"auto_1"`, `"quick"`), `name` | a save was written |
| `engine.game.loaded` | `slot` | a save's state is back, as its scenes load |

---

## Debugging

- Debug builds write plain JSON: open any `.arcsave` in a text editor.
- The log names every save written, every backup used and why a save was
  refused (`SaveManager:` lines).
- `ToString(slot)` gives the slot's name for your own logs.
- The demos: F5 / F9 quicksave / quickload, Esc for the pause menu's Save /
  Load, and every demo autosaves on scene changes. In the Triggers demo,
  collected coins stay collected; in every demo the player and crates keep
  their place.
