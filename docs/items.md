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
| `drop_prefab` | The prefab it becomes when dropped into the world; default the stock pickup (a later slice). |
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
| `InventoryRules::Grid(w, h)` | Minecraft | a grid of cells, one stack each; an item takes the cells of its `size`; optional sections (a hotbar) - see [Grids](#grids) |
| `InventoryRules::Survival(w, h, max)` | survival games | a grid, and a carry weight limit |
| `{ .Slots = 20 }` | an old-school list | up to 20 entries; a full stack starts another |

| Rule | Meaning |
|------|---------|
| `Slots` | the most entries; 0 = no limit |
| `OneEntryPerItem` | more of an item joins its entry instead of starting another |
| `UnlimitedStacks` | counts ignore items' `stack` |
| `MaxWeight` | carry weight; 0 = no limit |
| `OverWeight` | `Refuse` (default): what would go over isn't taken; `Allow`: it is, and `IsOverWeight()` says so (encumbered) |
| `QuickSlots` | how many quick slots (below) |
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
- **Quick slots** are references to items, not the items: a slot keeps its
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
- **Sections** are small grids beside the main one - a hotbar, a quiver -
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


