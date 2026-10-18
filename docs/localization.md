# Localization

Every piece of player-visible text is authored as a **key**
(`"ui.pause.title"`, `"demo.shop.buy"`) and shown in the player's language.
English is required; other languages are files dropped next to it. The player
picks the language in the **Game** settings screen, and everything on screen
follows at once.

- [Locale files](#locale-files)
- [Looking strings up](#looking-strings-up)
- [Placeholders and plurals](#placeholders-and-plurals)
- [Numbers and dates](#numbers-and-dates)
- [Text that follows the language](#text-that-follows-the-language)
- [The language](#the-language)
- [Fonts and scripts](#fonts-and-scripts)
- [Lua](#lua)
- [Tools](#tools)
- [Writing for translation](#writing-for-translation)

---

## Locale files

One JSON file per language, named by its code (`en.json`, `fr.json`,
`ja.json`, `pt-BR.json`), in two folders:

| Folder | Holds |
|---|---|
| `assets/engine/locale/` | the engine's strings (menus, settings, saves) - English, French, Japanese ship with it |
| the game's folder (`ApplicationConfig::LocaleFolder`, default `assets/locale/`) | the game's strings, and overrides of the engine's |

The game's file wins where both define a key. Nested objects join their keys
with dots, so these are the same:

```json
{ "ui": { "pause": { "title": "PAUSED" } } }
{ "ui.pause.title": "PAUSED" }
```

A `"$locale"` block describes the language:

```json
"$locale": { "name": "日本語", "fonts": ["../fonts/NotoSansJP-Regular.otf"] }
```

`name` is shown in the language list, written in that language. `fonts` are
fallback font files the language needs, relative to the locale file
([Fonts](#fonts-and-scripts)). Each file can carry its own block: the engine's
`ja.json` names Japanese, the demo's `ja.json` adds the font it ships.

Files are UTF-8. Values that aren't strings are ignored.

## Looking strings up

```cpp
label->Text = Loc::Get("ui.saves.none");
```

`Get` looks in the active language, then the language it's a region of
(`pt-BR` -> `pt`), then English, and returns the **key itself** if none has it -
a half-translated game shows English, never a blank, and a missing key is
visible on screen. Debug builds log each missing key once. Plain text passed to
`Get` comes back unchanged, so data fields can hold a key or text.

## Placeholders and plurals

```cpp
Loc::Format("ui.saves.auto", { { "index", 3 } });                    // "Autosave {index}" -> "Autosave 3"
Loc::Format("demo.scripted.chest_found", { { "count", gold } });     // picks the plural form
```

- **Placeholders are named** (`{index}`), never positional, so a translator can
  move them: `"Sauvegarde auto {index}"`, `"{scene}      Temps de jeu {time}"`.
- `{{` and `}}` are literal braces. A placeholder with no argument stays as
  written (`{index}`), so a mistake shows instead of vanishing.
- **Plurals**: with a number named `count`, a key with plural forms picks one
  by the language's rules (Unicode CLDR):

  ```json
  "demo.scripted.chest_found": { "one": "Found {count} gold coin!", "other": "Found {count} gold coins!" }
  ```

  The forms are `zero`, `one`, `two`, `few`, `many`, `other` - each language
  uses its own set, and every language has `other`. English and most of
  Europe: `one` / `other`. French and Brazilian Portuguese count 0 and 1.5 as
  `one`. Russian, Ukrainian, Polish: `one` / `few` / `many`. Czech, Slovak:
  `one` / `few` / `many` / `other`. Arabic: all six. Japanese, Chinese,
  Korean: `other` only. Other languages count like English.
- `Loc::Interpolate(text, args)` fills placeholders in text that isn't a key.

Glue nothing together in code: `"Slot " + n` can't be reordered, `"Slot {index}"` can.

## Numbers and dates

A number in a placeholder takes the language's separators: `{count}` with
12345 shows `12,345` in English, `12 345` in French. A spec after a colon
changes that:

| Placeholder | 1234.5 / a Unix time, in English | in French |
|---|---|---|
| `{n}` | `1,234.5` | `1 234,5` |
| `{n:n2}` | `1,234.50` (fixed decimals, 0-9) | `1 234,50` |
| `{n:raw}` | `1234.5` (as passed - a year, an id) | `1234.5` |
| `{saved:date}` | `Oct 18, 2026` | `18 oct. 2026` |
| `{saved:time}` | `5:42 PM` | `17:42` |
| `{saved:datetime}` | `Oct 18, 2026, 5:42 PM` | `18 oct. 2026, 17:42` |

Text arguments are never reformatted. `Loc::FormatNumber(value, decimals)`
and `Loc::FormatDate(unixSeconds, "date" | "time" | "datetime")` do the same
outside a string.

The style is part of each language's file, as ordinary keys (so a regional
language inherits its base's, and English's are the fallback):

| Key | English | Notes |
|---|---|---|
| `loc.decimal`, `loc.group` | `.` `,` | French: `,` and U+202F |
| `loc.group_from` | `4` | digits before grouping starts: Spanish and Polish use `5` (`1234`, `12 345`) |
| `loc.date` | `{month_short} {day}, {year}` | fields: `year`, `month`, `month2`, `month_name`, `month_short`, `day`, `day2` |
| `loc.time` | `{hour12}:{minute} {ampm}` | fields: `hour`, `hour2`, `hour12`, `minute`, `second`, `ampm` |
| `loc.datetime` | `{date}, {time}` | |
| `loc.am`, `loc.pm` | `AM`, `PM` | |
| `loc.month.1` .. `12`, `loc.month_short.1` .. `12` | `January`, `Jan` ... | |

Dates are the player's local time.

## Text that follows the language

Widgets keep their key and re-read it when the language changes:

| In a layout (`.arcui`) | In C++ |
|---|---|
| `"text_key"` (Label, Button, NineSliceButton) | `label->SetTextKey("ui.x")` |
| `"label_key"` (Checkbox) | `checkbox->SetLabelKey("ui.x")` |
| `"placeholder_key"` (TextInput) | `input->PlaceholderKey = "ui.x"` |
| `"item_keys": [...]` (Dropdown, RadioGroup) | `dropdown->ItemKeys = { ... }` |

Text a screen composes itself (a `Loc::Format` result) is rebuilt in the
screen's `OnLocaleChanged()`. Anything else can listen for
`engine.locale.changed` (`from`, `to`) - the demo retitles its window that way.

Data the engine shows reads keys too: input action names and categories on the
Controls screen (`RegisterAction(id, "Jump", "input.jump", "input.category.movement")`),
and every line of the credits file.

## The language

- **Starting language**: `ApplicationConfig::Language` if set, else the
  player's saved choice, else the first of the OS's preferred languages the
  game has (`fr-CA` matches `fr`), else English.
- **Switching**: `Application::SetLanguage("fr")` - the choice is saved, the
  language's fonts load, `engine.locale.changed` fires, and every screen
  follows. The Game settings screen (`ShowGameSettingsScreen`, the **Game**
  button in the title and pause menus) does this from a dropdown of every
  language, each named in itself.
- `Loc::GetLocales()`, `Loc::GetLanguage()`, `Loc::GetGeneration()` (changes
  whenever strings do).

## Fonts and scripts

Fonts cover most European languages out of the box (Latin-1, Latin
Extended-A, common punctuation). Anything else - Japanese, Chinese, Cyrillic -
is drawn the first time it's needed, from the font or its **fallbacks**: every
language's `"fonts"` become fallbacks of every font, the active language's
first. A character no font has draws as a box. See
[gui.md](gui.md#fonts-and-text).

Line wrapping breaks between Chinese / Japanese characters (which have no
spaces) and never before closing punctuation. Right-to-left text (Arabic,
Hebrew) and complex shaping (Thai, Hindi) aren't supported yet - planned as
Phase 42.

French typography wants a narrow no-break space (U+202F) before `!`, `?`, `;`
and `%`, and a no-break space (U+00A0) before `:`; fonts that lack them get
them drawn.

## Lua

```lua
Loc.Get("ui.pause.title")
Loc.Format("demo.shop.buy", { cost = 5 })
Loc.Format("demo.scripted.chest_found", { count = gold })
Loc.Language(), Loc.Languages(), Loc.SetLanguage("ja")
self.Screen:Find("title").TextKey = "demo.shop.title"
```

See [scripting.md](scripting.md#loc). The demo's toasts carry a key and its
arguments (`{ text = "demo.scripted.chest_found", count = 25 }`) and the HUD
formats them, so whoever sends one needn't know the language.

## Tools

**Console** (debug): `lang` lists the languages, `lang fr` switches,
`loc.missing [code]` lists what a language still lacks.

**Hot reload** (debug): edit a locale file while the game runs and the text on
screen follows within a second.

**Pseudo-locale** (debug): `qps` - every English string accented, padded about
35% and bracketed (`[Šáví Ğáñé ~~~~]`). Pick it in the Game settings (or
`lang qps`): text that isn't localized stays plain English, and text that
won't fit a longer translation loses its closing bracket.

**`arcbit-loc`** (built with the engine):

```bash
arcbit-loc check --locales engine/assets/locale game/assets/locale --scan engine/src engine/assets game/src game/assets
arcbit-loc coverage --locales engine/assets/locale game/assets/locale
arcbit-loc template --locales engine/assets/locale game/assets/locale --out game/assets/locale/de.json
```

`check` reports keys the content uses that English lacks (exit code 1 if
any), raw text in layouts, English keys nothing seems to use (a list to look
over: keys assembled at run time can't be seen), and each language's coverage.
`template` writes a new language's starting file: every English key with its
English text to replace. A test runs `check` over the engine and the demos.

## Writing for translation

- Give translators whole sentences with placeholders, not fragments.
- Leave room: translations often run 30% longer (German, French). Check
  layouts in `qps`.
- Keys name the place and meaning, not the English (`"ui.saves.overwrite"`,
  not `"overwrite_this_save"`).
- Text in code is a key; text in data (scene properties, Lua properties,
  credits) can be a key too - the code that shows it calls `Loc::Get`.
