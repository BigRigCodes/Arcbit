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
