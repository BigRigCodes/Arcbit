# Items

What the party carries. An item is a definition in a data file - its name and
description, icon, type, how many stack, weight, size in a grid, the stats it
gives when worn, what using it does. Inventories, the party, pickups, loot and
the inventory screen build on these (Phase 29, in progress).

- [Item files](#item-files)
- [Fields](#fields)
- [Building on another item](#building-on-another-item)
- [Text and translation](#text-and-translation)
- [Checks and reloading](#checks-and-reloading)
- [From C++](#from-c)
- [Inventories](#inventories)
  - [Grids](#grids), [Changing bags](#changing-bags)
- [The game's bags](#the-games-bags)
- [Containers](#containers)
- [Pickups and drops](#pickups-and-drops)
- [Loot tables](#loot-tables)
- [Currencies](#currencies)
- [Stats](#stats)
- [The party](#the-party) - [Equipment](#equipment), [Using items](#using-items)
- [The inventory screen](#the-inventory-screen)
- [In dialogs, scripts and the console](#in-dialogs-scripts-and-the-console)

---

## Item files

Items live in `.arcitems` files in the game's item folder (`assets/items`,
`ApplicationConfig::ItemFolder`) and below, read when the game starts. A file
is a JSON object of items by id; one file can hold a whole family, or one
item - ids are global either way, and the same id twice is an error (the
first keeps it). Comments are allowed.

```json
{
    "potion": {
        "name": "Potion", "description": "Restores 50 HP.",
        "icon": "assets/items/icons.json#potion",
        "type": "consumable", "category": "medicine", "weight": 0.5, "value": 25,
        "use": { "stats": { "HP": 50 } }
    },
    "iron_sword": {
        "name": "Iron Sword", "type": "equipment", "slot": "weapon",
        "size": [1, 2], "weight": 6, "stats": { "ATK": 5, "SPD%": -5 }
    },
    "old_boot": { "name": "Old Boot", "tags": ["junk"], "weight": 1.5 }
}
```

The demo's are in `game/assets/items/demo.arcitems`.

---

## Fields

| Field | Meaning |
|-------|---------|
| `name`, `description` | The English text (see below). `name` is required. |
| `icon` | A texture path, or a sprite sheet and one of its frames (`"icons.json#potion"`). |
| `type` | `consumable`, `equipment`, `key` (can't be dropped or sold), `material` (the default), `currency`, or a game's own. |
| `category` | Pockets and filters in the inventory; default: the type. |
| `tags` | Anything else, for a game's own rules. The engine knows `junk`: sorted last, offered first when the player has to discard. |
| `stack` | The most in one stack. Default by type, per project: `ApplicationConfig::Defaults.ItemStack` (consumable 99, equipment 1, key 1, material 999, currency 999999), and `ItemStackOther` (99) for a game's own types. |
| `weight` | Carry weight of one (default 0). |
| `value` | A price, in the project's main currency. |
| `size` | `[width, height]` in a grid inventory's cells; default `[1, 1]`. |
| `rotatable` | Whether a grid may turn it to fit; default `true`. |
| `slot` | Equipment: the kind of slot it fits - one of the project's `Defaults.EquipmentSlots` kinds (weapon, offhand, head, body, accessory by default; two accessory slots take `accessory`). |
| `stats` | Equipment: added to the wearer's stats - flat (`"ATK": 5`) or percent (`"SPD%": -5`). |
| `use` | What using it does (a later slice). |
| `drop_prefab` | The prefab it becomes when dropped into the world; default the stock pickup ([Pickups and drops](#pickups-and-drops)). |
| `properties` | A game's own fields, read back as JSON. |
| `extends`, `abstract` | See below. |

---

## Building on another item

`"extends": "potion"` starts from another item, in any file: the item's own
fields go over the parent's, nested ones (`stats`, `use`, `properties`) field
by field, and `null` removes one the parent has. An `"abstract": true` item
is only for extending - a family's shared fields - and isn't an item itself.

```json
"potion_base": { "abstract": true, "type": "consumable", "category": "medicine", "weight": 0.5 },
"potion":      { "extends": "potion_base", "name": "Potion", "use": { "stats": { "HP": 50 } } },
"hi_potion":   { "extends": "potion", "name": "Hi-Potion", "use": { "stats": { "HP": 150 } } }
```

---

## Text and translation

The English lives in the item file, as a dialog's does. Each item's name and
description get Loc keys from its id - `items.<id>.name`,
`items.<id>.description` - and the library gives Loc the English as it reads
the files, so the translations go in the usual locale files:

```json
"items.potion.name": "Potion",
"items.potion.description": "Rend 50 PV."
```

`arcbit-loc --items <folder>` reads item files the way `--dialogs` reads
dialogs: their names count in coverage and templates, their problems are
reported (errors fail `check`), and English a locale file replaces is noted
([localization.md](localization.md)).

---

## Checks and reloading

Problems are logged with the item and its file as the files are read:

- **Errors**: no `name`, a duplicate id, `extends` to an item that doesn't
  exist or round in a loop (that item doesn't load), a bad `size`, a `stack`
  below 1, a negative `weight`, a file that isn't valid JSON.
- **Warnings**: an unknown field (a misspelling), equipment without a `slot`,
  a `slot` no equipment slot takes, a `slot` on something that isn't
  equipment, an icon file that doesn't exist.

Debug builds read an edited item file again within a second.

---

## From C++

```cpp
const ItemDef* sword = GetApp().GetItems().Find("iron_sword");
if (sword && sword->Type == ItemTypes::Equipment) { ... }
Loc::Get(sword->NameKey); // its name in the player's language
```

`ItemLibrary` reads the folders (`AddFolder`), checks them against its
`ItemRules` (stack sizes, slot kinds, where icons are), and keeps every
item's issues (`GetIssues`) for tools.

---

## Inventories

An `Inventory` holds stacks of items (`ItemStack`: an item id and a count)
and the rules of the kind of bag it is. A game picks a kind per inventory;
the presets are starting points, and the rules mix freely:

| Preset | Like | Rules |
|--------|------|-------|
| `InventoryRules::Pockets()` | Pokemon | one entry per item, up to its stack size; the screen shows a list per category |
| `InventoryRules::Weight(max)` | Skyrim | one entry per item, any count (stack sizes ignored), a carry weight limit; category tabs filter it |
| `InventoryRules::Grid(w, h)` | Minecraft | a grid of cells, one stack each; an item takes the cells of its `size`; sections only if added - see [Grids](#grids) |
| `InventoryRules::Survival(w, h, max)` | survival games | a grid, and a carry weight limit |
| `{ .Slots = 20 }` | an old-school list | up to 20 entries; a full stack starts another |

| Rule | Meaning |
|------|---------|
| `Slots` | the most entries; 0 = no limit |
| `OneEntryPerItem` | more of an item joins its entry instead of starting another |
| `UnlimitedStacks` | counts ignore items' `stack` |
| `MaxWeight` | carry weight; 0 = no limit |
| `OverWeight` | `Refuse` (default): what would go over isn't taken; `Allow`: it is, and `IsOverWeight()` says so - the party is encumbered ([below](#pickups-and-drops)) |
| `QuickSlots` | how many quick slots - the hotbar, for any kind (below); 0, none, by default |
| `Width`, `Height`, `Sections` | a grid (both above 0; `Slots` is then unused) and its sections |

```cpp
Inventory bag(app.GetItems(), InventoryRules::Pockets());
const u32 added = bag.Add("potion", 5);   // as many as fit: its stacks first, then a new entry
bag.Remove("potion", 2);                  // newest stacks first
if (bag.Has("cellar_key")) ...
```

- **Adding** takes as many as fit and says how many (`Room` asks without
  adding): slots, stack sizes and carry weight can each stop it partway.
  An id that isn't an item is refused, logged.
- **Arranging**: `Move` an entry to another place, `Split` some off into a
  new entry after it (not with one entry per item), `Merge` one entry into
  another of the same item, `RemoveAt` from one entry.
- **Sorting** (`Sort`) by name (in the player's language), type, category,
  value, weight, count or recent, either way; `junk` items always sort last,
  and ties go by name. A grid repacks (below).
- **Filtering** (`Filter`) keeps entries by category, type, tag or text in
  the name (any case), returning their indices in order.
- **Quick slots** are the hotbar, and any kind of bag can have them - or not:
  none unless the game sets `QuickSlots`. They're references to items, not
  the items: a slot keeps its
  item while the bag has none and shows 0 (`SetQuickSlot`, `GetQuickSlots`).
- **Changes** reach `OnChange(item, delta)`.
- **Saving**: `ToJson` / `FromJson` write the stacks (in a grid, where each
  lies), quick slots and the bag's own rules (a bag the player upgraded stays
  upgraded); a saved stack of an item that no longer exists is dropped,
  logged.

### Grids

A grid bag has `Width` x `Height` cells. Each stack lies at a cell (`X`,
`Y`, its top-left) and covers the cells of its item's `size`, upright or
turned a quarter (`Rotated`: width and height swap) - one stack per space,
up to the item's stack size.

- **Placing**: a new stack goes in the first space it fits, row by row,
  upright first, then turned (unless the item has `"rotatable": false`).
- **Sections** are small grids beside the main one, only when a game adds
  them - a quiver, or a Minecraft-style hotbar of real slots (quick slots
  are the hotbar every kind can have) -
  each with a name, a size and optionally the one category it takes. New
  stacks try the main grid first, then each section in order:

  ```cpp
  InventoryRules rules = InventoryRules::Grid(9, 3);
  rules.Sections = { { .Name = "hotbar", .Width = 9, .Height = 1 },
                     { .Name = "quiver", .Width = 1, .Height = 1, .Category = "ammo" } };
  ```

- **Moving**: `MoveTo(index, section, x, y, rotated)` puts an entry at a cell
  if it fits there (false otherwise); `Rotate` turns it in place; `Swap`
  trades two entries' places if each fits in the other's; `StackAt` says what
  covers a cell; `Footprint` gives the cells an entry takes as it lies.
  `Split` puts the new part in the first space it fits.
- **Sorting** repacks the main grid in the sorted order (sections keep the
  player's arrangement). If that order leaves a big item no space, it packs
  largest-first instead; if that fails too, nothing moves and `Sort` returns
  false.
- **Loading** a saved grid whose stacks no longer fit where they were (an
  item grew in an update): the first to claim a cell keeps it, the rest move
  to the first space they fit - dropped, logged, only if there is none.

### Changing bags

A bigger grid, more carry weight, a different kind altogether:
`Convert(rules)` moves everything across - in a grid, each stack where it was
if it still fits there - keeping the bag's order. When the new bag holds less
(a smaller grid, a weight limit where there was none), it refuses until
enough is gone:

```cpp
const std::vector<ItemStack> mustGo = bag.ConvertOverflow(bigger);
if (mustGo.empty()) bag.Convert(bigger);
else ShowDiscardPrompt(mustGo);           // junk is what it lists first
```

`ConvertOverflow` lists what wouldn't fit, filling the new bag with junk
last, so junk is what's left out first - the player is offered that to
discard (and may pick other things). If keeping stacks where they were
leaves something out, a fresh largest-first packing is tried too, and the one
that leaves out less that isn't junk wins.

---

## The game's bags

The party's things are game state, like `SaveData` - not part of any scene
(every scene builds its own player entity). `Application::GetInventories()`
holds the game's bags by name: **`party`**, the party's shared bag, exists from
the start with the project's kind (`ApplicationConfig::Defaults.PartyBag`,
pockets by default); a game adds its own (`Create("stash", rules)`, or
`AddStarting` for one every new game begins with).

They're saved as the save's `inventories` section - every bag, its rules and
its stacks ([saves.md](saves.md)). A new game starts them over: empty, with
the rules they began with. Every change to a bag publishes
`engine.item.added` / `engine.item.removed` (`bag`, `item`, `count`).

---

## Containers

A scene entity that holds items - a chest, a crate - has a `Container`: its own
bag, saved with the scene's state like any component (make it `Persistent`).

```json
"chest": {
    "Container": { "items": { "potion": 2, "cellar_key": 1 }, "currency": { "gold": 25 },
                   "rules": { "kind": "list", "slots": 8 }, "open": "take_all" }
}
```

| Field | Meaning |
|-------|---------|
| `items` | its contents, `{ id: count }`, in that order |
| `currency` | amounts of the game's currencies, `{ id: amount }` |
| `rules` | the kind of bag (an `InventoryRules` as JSON: `kind`, `slots`, `one_entry_per_item`, `max_weight`, `width` / `height`...); default a plain list |
| `open` | `transfer` (the transfer screen, the bag beside it) or `take_all` (everything into the party's bag at once); default the project's `Defaults.ContainerOpen` (transfer) |
| `loot` | a [loot table](#loot-tables) rolled into it the first time it's opened |

Saves write a container as `stacks` (what it holds now) rather than `items`.

**Opening.** The player uses a container like any `Interactable` (one is
added if it has none), or code calls `Application::OpenContainer(entity,
by)`. The first time, its `loot` is rolled into it and cleared - so it's
rolled once, and what it rolled is saved. Then, `take_all` moves everything
that fits into the party's bag, and all its currency; what doesn't fit
stays. `transfer` is for the transfer screen, which comes with the inventory
screens - until then it only rolls the loot. Either way
`engine.container.opened` says so (`mode`; with `take_all`, `taken` and
`left`).

## Pickups and drops

An item lying in the world is an entity with an `ItemPickup`. A party entity -
one with a `PartyLink`, like the player - picks it up by walking onto it
(within half a tile of its center), or, with `"on": "interact"`, by using it
like any `Interactable`. What fits goes into the bag; the entity goes once
it's all taken.

```json
"potions": {
    "ItemPickup": { "item": "potion", "count": 2 },
    "Persistent": {},
    "Transform2D": { "position": [64.0, 0.0] }
}
```

| Field | Meaning |
|-------|---------|
| `item`, `count` | what lies there (default 1) |
| `on` | `touch` (default) or `interact` - given an `Interactable` if it has none |
| `bag` | the bag it goes into; default `party` |
| `reach` | how near a touch counts, in pixels from its center; default half a tile |

- **Make it `Persistent`** so it stays picked up when the scene reloads and
  in saves.
- **Its look**: without a `SpriteRenderer` it's drawn as the item's `icon`
  (a texture, or `"sheet.json#frame"`; a white square if it has none), half a
  tile across. Give it a `SpriteRenderer` (and scale) for a look of its own.
- **When it doesn't all fit**, what fit is taken and the rest stays, with
  `engine.item.not_picked_up` (`reason`: `no_room`) - a toast's cue. It
  isn't tried again until the player steps off and back on; nor is a dropped
  item, from under the player's feet.
- **A currency** picked up (`"item": "gold"`) counts however that currency
  is kept.

**Dropping** - `Application::DropItem(item, count, bag)`, the console's `drop`,
Lua's `Inventory.Drop` - takes items out of a bag and puts them on the ground
where the party's leader stands, as the item's `drop_prefab` or else the stock
`pickup` prefab (`Tag` Pickup, `ItemPickup`, `Persistent`; a game's own
`pickup.arcprefab` replaces it). Key items can't be dropped. A dropped pickup
is saved with the scene like any spawned persistent entity
([saves.md](saves.md)).

**Encumbered.** When a bag's rules allow going over its carry weight
(`OverWeight = Allow`) and it's over, the party entities using it walk at the
project's `Defaults.EncumberedSpeed` (half speed by default) - through
`MoveIntent::SpeedScale`, so every movement style slows. The change is
announced with `engine.party.encumbered` (`encumbered`: true / false).

C++: `Scene::PickUp(pickup, by)` picks one up directly. A scene's pickups go
through its `ItemHost` (what `Take`s the items, what `Dress`es a bare
pickup), which `Application` gives every scene; a scene without one - in a
test, say - leaves its pickups lying.

## Loot tables

What a chest holds or an enemy leaves, rolled. Loot tables live in
`assets/loot/*.arcloot` (`ApplicationConfig::LootFolder`), many to a file,
by id:

```json
{
    "slime": {
        "always":  [ { "currency": "gold", "count": [1, 5] } ],
        "rolls":   [1, 2],
        "entries": [ { "item": "potion", "weight": 3 },
                     { "item": "old_boot", "count": [1, 2] },
                     { "table": "gems" },
                     { "weight": 6 } ]
    }
}
```

| Field | Meaning |
|-------|---------|
| `always` | entries given every time the table is rolled |
| `rolls` | how many weighted picks from `entries` - a number or `[min, max]`; default 1 |
| `entries` | the picks: each has its `weight` (default 1) against the others |

An entry gives an `item`, a `currency` or another `table` (rolled `count`
times), or - with none of them - nothing: `{ "weight": 6 }` above makes
empty-handed the likeliest pick. `count` is a number or `[min, max]`
(default 1). The same item from several entries comes back as one stack.

Once the items are read, every table is checked: an unknown item, currency
or table is an error, as is a table that rolls itself (`a -> b -> a`),
logged with its file; a misspelled field is a warning.

**Where loot goes:**

- **Containers** with `"loot"` roll it the first time they're opened ([above](#containers)).
- **`LootDrop`** - `"LootDrop": { "table": "slime" }` on an enemy, a barrel:
  when the entity is destroyed, its table is rolled and scattered around
  where it was as pickups (each stack one, currencies too). Not when its
  scene unloads, and not when a save destroys it again - that loot was
  dropped the first time.
- **Code**: `Application::RollLoot(table)` gives the stacks
  (`LootStack`: `Id`, `Count`, `Currency`) without giving them to anyone;
  `GetLoot().Roll(table, rng)` with your own `std::mt19937` for a seeded,
  repeatable roll. Lua: `Loot.Roll(table)`.

The console's `loot` lists the tables; `loot <table> [times]` rolls one,
and many times over tallies the average - a quick check of the odds.

---

## Currencies

The project lists the game's currencies (`Defaults.Currencies`; `gold` by
default). Each is either:

- **a counter** in the save data (the default) - `"gold"`, as dialogs and
  `Save.Get` have always seen it; or
- **an item** (`{ .Id = "coin", .Item = true }`) - stacks of the item with
  that id in a bag (the party's, or `Bag`), carried, dropped and lost like any
  item.

`Application::GetCurrencies()` reads and pays them the same way whichever
kind: `Get`, `Add` (as much as can be - a counter never goes below 0, an item
only what fits) and `Spend` (all or nothing). Changes made through it publish
`engine.currency.changed` (`currency`, `amount`, `change`).

---

## Stats

A character's numbers. The engine has ten - `HP`, `MaxHP`, `MP`, `MaxMP`,
`ATK`, `DEF`, `MAG`, `RES`, `SPD`, `LUK` - and a game's stats file
(`assets/stats.json`, `ApplicationConfig::StatsFile`) adds to them, changes
them, or replaces them all:

```json
{ "replace": false,
  "stats": {
    "STR":        { "name": "Strength", "default": 10, "min": 0, "max": 99 },
    "Stamina":    { "name": "Stamina", "default": 50, "max_stat": "MaxStamina" },
    "MaxStamina": { "name": "Max Stamina", "default": 50 },
    "ATK":        { "default": 8 } } }
```

| Field | Meaning |
|-------|---------|
| `name` | English (`stats.<id>.name`); the engine's stats have theirs in the engine's locale files - rename one in your own locale file |
| `default` | a character's value when nothing sets it |
| `min`, `max` | kept within (min 0; no max unless given) |
| `max_stat` | a **resource**: it has a current value that can't pass this stat (HP up to MaxHP) - spent and restored, rather than a character's ability |

`Stats` (a value, and a component for enemies and NPCs: `"Stats": { "base":
{ "ATK": 14 }, "current": { "HP": 30 } }`) holds a character's own numbers:
`Base` (a stat not there is its default) and `Current` for resources (one not
there is full). `StatSet::Effective` works out every stat: base, plus what
equipment adds - flat first (`"ATK": 5`), then percent (`"SPD%": -20`) -
within min and max; a resource is its current value, at most its maximum's.

---

## The party

Who's in the party is game state, like the bags - `Application::GetParty()`,
saved as the `party` section. The game's party file (`assets/party.json`,
`ApplicationConfig::PartyFile`) says who can join and who's there when a
game starts:

```json
{ "starting": ["hero"],
  "members": {
    "hero": { "name": "Hero", "stats": { "MaxHP": 120, "ATK": 12 }, "equipment": { "weapon": "iron_sword" } },
    "ayla": { "name": "Ayla", "stats": { "MAG": 18 }, "bag": "ayla" } } }
```

Without one, the party is `player` alone. A member's `name` is English
(`party.<id>.name`); `stats` go over the stats' defaults; `equipment` is
what they wear when they first join; `bag` gives them a bag of their own
instead of the party's shared one.

- `Join(id)` / `Leave(id)` (what a leaver wore goes to their bag) /
  `Move(id, index)`. The first member leads - the one the scene's player
  entity shows: a `PartyLink` component (`"PartyLink": {}` - the leader; or
  `{ "member": "ayla" }`).
- `GetStats(id)`: every stat, with equipment.
- Events: `engine.party.joined` / `engine.party.left` (`member`).

### Equipment

The project's equipment slots (`Defaults.EquipmentSlots`: weapon, offhand,
head, body, accessory1, accessory2 - each with the kind of item it takes; an
item's `slot` names a kind).

- `Equip(member, item, slot)` takes the item from the member's bag and wears
  it: in `slot` if given, else the first free slot of its kind, else the
  first of its kind - what was there goes back to the bag. It refuses
  anything that isn't equipment, an item the bag hasn't got, and a swap
  whose old item won't fit back.
- `Unequip(member, slot)` puts it back in the bag (refused if the bag is
  full).
- Events: `engine.item.equipped` / `engine.item.unequipped` (`member`,
  `item`, `slot`).

Equipment's bonuses aren't copied anywhere: effective stats are worked out
when asked, so they're never out of step with what's worn.

### Using items

`Application::UseItem(item, member, bag)` runs the item's `use` block on a
member (the leader when left out), from a bag (the party's):

```json
"use": { "stats": { "HP": 50 }, "add": { "gold": 5 }, "event": "game.drank",
         "sound": "assets/sfx/drink.wav", "consume": true }
```

| Key | Does |
|-----|------|
| `stats` | changes the member's stats: a resource's current value (within its maximum), else their own base for good |
| `set`, `add` | save data, as in a dialog (`add` on a currency goes through the currencies) |
| `event` | publishes a game event, with `item` and `member` |
| `lua` | runs a Lua statement |
| `sound` | plays a sound |
| `consume` | whether one is used up; default: yes for consumables |

It returns how it went (`UseResult`): `Used`; or, with the item kept,
`NotInBag`, `NotUsable` (no `use`), `NoMember`, or `NoEffect` - its `stats`
would change nothing (a potion at full HP) and it does nothing else (`set`,
`add`, `event` and `lua` always count as doing something). Publishes
`engine.item.used` (`item`, `member`, `bag`) when used.

---

## The inventory screen

`Application::OpenInventory(bag)` (default the party's) opens `InventoryScreen`
as a menu; so do the console's `inventory [bag]` and Lua's
`Inventory.Open(bag)`. This is the screen for the list kinds - pockets,
weight; the grid kinds get their own view next.

- **Tabs**: pockets have one per item category the game has (empty ones too,
  as a Pokemon bag's pockets are); other kinds have **All**, then the
  categories the bag holds. Click one, or **Q** / **E** (the shoulder buttons).
- **Rows**: icon, name, count - and weight, in a bag with a carry limit. Focus
  (arrows, D-pad) or a click selects a row; **Enter**, or a click on the
  selected row, uses it (if it has a `use`) or else equips it. Dragging a row
  onto another moves it; onto an equipment slot, wears it.
- **Sort** cycles name, type, value, weight, count, recent (it sorts the bag
  itself); the **filter** keeps rows whose names match.
- **Details**: the selected item's icon, name, weight and value,
  description, the stats it gives worn ("Attack +5"), what using it does
  ("Use: HP +50"), and - for equipment - the member's stats that wearing it
  would change ("Attack: 10 → 15", better green, worse red). **Use**,
  **Equip** and **Drop** act on it (Drop: not key items; it lands at the
  leader's feet), each leaving a message.
- **Equipment**: a member's slots (`<` / `>` change member); click a slot to
  take off what's in it, drop an item on one to wear it; their stats below.
- **Footer**: the carry weight (a bar, red when over), the currencies, and a
  help line naming the keys bound now - a gamepad's buttons once it's in use.
- The screen follows the bag: a pickup or a script changing it while it's
  open redraws it.

Names come from the locale files: categories `items.category.<id>`,
equipment slots `equipment.slot.<name>`, currencies `currency.<id>.name`,
members `party.<id>.name`. One a file lacks shows as its id, tidied
(`key_items` -> "Key items"), and logs a warning the first time - a game's
own categories need their names in every language it ships. The engine has
the stock types', slots' and `gold`'s.

**Layout and art.** The layout is `assets/engine/ui/inventory.arcui`
(`InventoryScreen::LayoutPath`, replaceable like the dialog box's) and the art
`assets/engine/ui/inventory/` - `panel.png` (the frame), `row.png` and
`tab.png` (drawn white, tinted per state), `slot.png` (an icon's well). The
screen fills these widgets when they're there: `title`, `filter`
(TextInput), `sort`, `tabs` (GridPanel), `list` (ScrollPanel), `empty`,
`weight-text`, `weight-bar`, `currencies`, `message`, `detail-icon`,
`detail-slot`, `detail-name`, `detail-info`, `detail-desc`, `detail-stats`,
`use`, `equip`, `drop`, `equipment`, `member-name`, `member-prev`,
`member-next`, `slots`, `member-stats`; and stamps the templates `tab`, `row`
(with `slot`, `icon`, `name`, `count`, `weight`) and `slot-row` (with `slot`,
`icon`, `item`). Rows are named `row-0`, `row-1`...; slot rows
`slot-<name>`. Meta `row_height`, `slot_row_height`.

## In dialogs, scripts and the console

**Dialogs** ([dialog.md](dialog.md)) check and change the party's bag:

```json
{ "id": "buy", "text": "A potion. (5 gold)", "if": { "gold": [">=", 5], "has": ["cellar_key"] },
  "add": { "gold": -5 }, "give": { "potion": 2 } }
```

`"has"` in an `"if"` (`{ "potion": 2 }` or a list of ids, one each), and the
actions `"give"` and `"take"`. Currencies work in conditions, `"add"` and
`{gold}` placeholders whichever kind they are; `{count.potion}` is how many
the party carries.

**Lua** ([scripting.md](scripting.md)):

| | |
|---|---|
| `Items.Get("potion")` | the item: `Id`, `Name` and `Description` (in the player's language), `Type`, `Category`, `Tags`, `Stack`, `Weight`, `Value`, `Slot`; nil if there's none |
| `Inventory.Add(item, count, bag)` | adds as many as fit, returns how many (bag optional: the party's) |
| `Inventory.Remove(item, count, bag)`, `Inventory.Count(item, bag)`, `Inventory.Has(item, count, bag)` | |
| `Inventory.Get(bag)` | `{ { Item = "potion", Count = 2 }, ... }` |
| `Inventory.Drop(item, count, bag)` | onto the ground where the leader stands; returns how many |
| `Loot.Roll(table)` | `{ { Id = "potion", Count = 2, Currency = false }, ... }` - given to no one |
| `Inventory.Open(bag)` | the inventory screen, on a bag (the party's when left out) |
| `Currency.Get(id)`, `Currency.Add(id, change)`, `Currency.Spend(id, amount)` | as above |
| `Party.Members()`, `Party.Leader()` | the members' ids, the leader first |
| `Party.Get(id)` | `{ Id, Name, Bag, Equipment = { weapon = "iron_sword" } }` |
| `Party.Stats(id)` | every stat, with equipment: `{ HP = 100, ATK = 15, ... }` |
| `Party.Join(id)`, `Party.Leave(id)` | |
| `Party.Equip(id, item, slot)`, `Party.Unequip(id, slot)` | slot optional for Equip |
| `Party.Use(item, member, bag)` | use an item (member: the leader, bag: the party's, when left out): `true`, or `false` and why - `"not_in_bag"`, `"not_usable"`, `"no_member"`, `"no_effect"` |

**Console**: `give <item | currency> [count] [bag]`, `take <item |
currency> [count] [bag]` (cheats); `drop <item> [count] [bag]` puts items
on the ground; `loot [table] [times]` lists or rolls loot tables;
`inventory [bag]` opens the inventory screen; `items [bag]` lists a bag and
the currencies; `party` lists the members, their stats and equipment; `equip
<member> <item> [slot]`, `unequip <member> <slot>`, `use <item> [member]`
(cheats).

