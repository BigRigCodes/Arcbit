# Dialog

Conversations: an NPC speaks lines in a box along the bottom of the screen,
with their name and portrait, the text typing out in their voice; the player
picks from choices; lines and choices can depend on the game's state and change
it. A conversation is a `.arcdialog` file - no code needed to write one or to
make an NPC say it.

The Scripted demo has two: Elder Maren (`game/assets/dialog/elder.arcdialog` -
a gold check, a gift, a map to buy, remembered next time, in English, French
and Japanese) and the guard, who knows whether you've met her.

- [Writing a dialog](#writing-a-dialog)
- [Conditions and actions](#conditions-and-actions)
- [Text, keys and translation](#text-keys-and-translation)
- [Starting a conversation](#starting-a-conversation)
- [While it runs](#while-it-runs)
- [The dialog box](#the-dialog-box)
- [From C++](#from-c)

---

## Writing a dialog

Dialog files live in the game's dialog folder (`assets/dialog`,
`ApplicationConfig::DialogFolder`) and below; a dialog's id is its path there
without the extension - `elder.arcdialog` is `"elder"`, `town/guard.arcdialog`
is `"town/guard"`. Every file is read when the game starts.

```json
{
    "speaker": "elder",
    "speakers": {
        "elder": { "name": "Elder Maren", "portrait": "assets/portraits/elder.png", "voice": "assets/sfx/voice_low.wav" }
    },
    "world": "run",
    "nodes": {
        "greet": [
            { "say": "Back again?", "if": { "met_elder": true }, "goto": "ask" },
            "Welcome, traveler. I am Maren.",
            { "say": "The wind is up tonight.", "speaker": null, "set": { "met_elder": true } },
            { "goto": "ask" }
        ],
        "ask": [
            { "say": "You have {gold} gold. What do you need?" },
            { "choice": [
                { "id": "map", "text": "A map, please. (5 gold)", "if": { "gold": [">=", 5] },
                  "add": { "gold": -5 }, "goto": "map" },
                { "id": "bye", "text": "Nothing. Goodbye.", "goto": "end" }
            ] }
        ],
        "map": [
            { "say": "Here. Mind the <shake>cliffs</shake>.", "set": { "has_map": true }, "event": "quest.map_bought" }
        ]
    }
}
```

| Key | Meaning |
|-----|---------|
| `speakers` | Who talks: an id each, with `name` (shown on the name plate), `portrait` (a texture path) and `voice` (a sound played as their text appears). A speaker's `key` names its Loc key instead of the default. |
| `speaker` | The speaker of lines that don't name one. |
| `world` | `"run"` (default): the world keeps moving while the conversation runs; `"pause"`: the scene stops too. |
| `start` | The node it starts at; default, the first. |
| `nodes` | Named lists of steps. |

A **node** runs its steps in order. Each step is one of:

- **A line** - `"say"`, or just a string (a line by the dialog's speaker). It
  waits for the player to move on. `"speaker"` names someone else, `null` makes
  it narration (no name plate, no voice); `"portrait"` swaps in another
  picture for this line (an expression).
- **A choice** - `"choice"`: a list of options, each with `text`, an optional
  `id` (what the end of the conversation reports), and its own `if`, actions
  and `goto`. An option whose `if` fails is hidden; if none shows, the step is
  skipped.
- **A branch** - `"branch"`: a list of arms with `if` and `goto`; the first arm
  that holds is taken, none - carry on.
- **Just actions** - a step with only actions and / or a `goto`.

Any step can carry an `"if"` (skipped unless it holds), actions (below) and a
`"goto"`: a node, or `"end"`. A line's `goto` is followed once the player moves
past it; an option's when picked (without one, the choice step's own goto, else
the next step). Running off the end of a node ends the conversation.

Mistakes are reported when the file loads - logged, with the step: a goto to a
node that doesn't exist, a line by an unknown speaker, an empty choice, a step
that does nothing, a text key used twice, a bad comparison. Misspelled fields
and nodes nothing leads to are warnings. A file with errors still loads as far
as it can.

---

## Conditions and actions

Most state a conversation reads and writes is the game's `SaveData`
([saves.md](saves.md)) - flags, counters, quest stages - so it's saved with the
game and visible to scripts (`Save.Get`).

**`if`** - every test must hold:

```json
"if": { "met_elder": true, "gold": [">=", 5], "quest.dragon": "started" }
"if": { "any": [ { "title": "knight" }, { "title": "lord" } ] }
"if": { "not": { "angry": true } }
"if": { "lua": "Save.Get('rep', 0) > 10 and Loc.Language() == 'en'" }
```

- A bare value tests equality; `[op, value]` compares, with `==`, `!=`, `<`,
  `<=`, `>`, `>=`. Numbers compare as numbers (whole or not); text compares
  alphabetically.
- A key that isn't saved yet reads as the tested value's zero - `false`, `0`
  or `""` - so flags need no setup.
- `any` holds if one of its conditions does; `not` if none of its do; `lua`
  if the Lua expression is true.

**Actions** run when a line shows, a choice is offered, or an option is
picked:

| Action | Does |
|--------|------|
| `"set": { "met_elder": true, "stage": 2 }` | sets save data |
| `"add": { "gold": -5 }` | adds to a number (missing counts as 0; whole numbers stay whole) |
| `"event": "quest.map_bought"` or `{ "name": ..., "payload": { ... } }` | publishes a game event ([events.md](events.md)) |
| `"lua": "Save.Set('trust', Save.Get('trust', 0) + 1)"` | runs a Lua statement |
| `"call": "Wave"` | calls `self:Wave()` on the script of the entity being talked to |

Dialog Lua runs in an environment of its own, with the full engine API
([scripting.md](scripting.md)); globals it sets last between conversations. An
error is reported like any script error, and a failing `lua` condition is
false.

---

## Text, keys and translation

The English lives in the dialog file, where it's written. Every line, option
and speaker name gets a Loc key, and the engine adds the English to Loc as the
file loads - so translations go in the usual locale files
([localization.md](localization.md)), and missing ones fall back to English.

| Text | Key |
|------|-----|
| a line or option with no `id` | `dialog.<id>.<node>.<n>` - `n` counts the node's unnamed text, from 1 |
| one with an `"id"` | `dialog.<id>.<node>.<that id>` |
| one with a `"key"` | exactly that |
| a speaker's name | `dialog.<id>.speaker.<speaker id>`, or its `"key"` |

A dialog in a folder uses dots: `town/guard`'s lines are `dialog.town.guard...`.
Only unnamed text is numbered, so adding a step with no text never renumbers
anything; once a line is translated, giving it an `id` keeps its key stable
whatever moves around it.

```json
"dialog.elder.greet.1": "Bienvenue, voyageur. Je suis Maren.",
"dialog.elder.ask.map": "Une carte, s'il vous plait. (5 or)"
```

Text can use **rich text** - `<b>`, `<i>`, `<color=gold>`, `<wave>`,
`<shake>`, `<rainbow>`, `<pause=0.4>`, `<speed=0.5>` and the rest
([gui.md](gui.md#rich-text)) - and **placeholders**: `{gold}` is filled from
save data, and a game can supply more (`DialogHost::Args`, e.g. `{player}`).
A save key that isn't set yet shows `0`, as it reads in a condition - so set a
text value (a title, a name) before a line uses it.
Number formatting and plural forms work as in any localized string.

The console's `loc.missing <lang>` includes dialog keys a language lacks, and
`arcbit-loc` reads dialog files (`--dialogs`): their lines in coverage and
translator templates, their problems reported, and translations of lines that
were renumbered or removed flagged ([localization.md](localization.md)).

---

## Starting a conversation

**From an NPC, no script:** give it a dialog in its `Interactable`, and talking
to it (the player's Interactor, facing it) starts it:

```json
"elder": {
    "Interactable": { "prompt": "demo.prompt.talk", "dialog": "elder", "dialog_node": "greet" }
}
```

**From Lua:**

```lua
function Elder:OnInteract(by)
    if Save.Get("quest.done", false) then
        Dialog.Start("elder", { Node = "thanks", By = by })
    else
        Dialog.Start("elder")      -- With: this entity, from its own hook
    end
end

function Elder:OnDialogEnd(id, choice)
    if choice == "map" then self.Entity:Set("SpriteRenderer", { tint = { 1, 0.9, 0.6, 1 } }) end
end
```

`Dialog.IsActive()` and `Dialog.Stop()` round it out. The scene script hears
every conversation's end as `OnDialogEnd(id, choice, with)`.

**From C++:** `app.StartDialog("elder")`, or with a `DialogRequest` (`Id`,
`Node`, `Scene`, `With` - the entity talked to, `By` - the player).
`IsDialogActive()` and `StopDialog()` too.

**From the console:** `dialog` lists the dialogs, `dialog <id> [node]` starts
one.

A conversation started while another runs **waits its turn**, and starts once
that one has ended. `StopDialog` / `Dialog.Stop` ends the current one and drops
the waiting ones.

---

## While it runs

- **The player and the NPC are held** (`Held`, `docs/ecs.md`): the player's
  input goes to the dialog box, the NPC stops walking - a tile mover finishes
  the step it's on - and turns to face the player. A script that moves an
  entity by hand should check `entity:IsHeld()`; `entity:Move` already does.
- **The world** keeps running, unless the dialog says `"world": "pause"`.
- **Autosaves wait** until it ends. Play time keeps counting.
- **A menu over it** (pause) freezes it: the typewriter and auto-advance wait
  until the menu closes.
- **Events** go out ([events.md](events.md)): `engine.dialog.started`
  (`id`, `with`), `engine.dialog.line` (`id`, `key`, `speaker`),
  `engine.dialog.choice` (`id`, `choice`) and `engine.dialog.ended` (`id`,
  `with`, `choice`).
- At the end, the NPC's script hears `OnDialogEnd(id, choice)` and the scene
  script `OnDialogEnd(id, choice, with)`, with the last choice's `id` (or
  `""`).

Debug builds reload an edited dialog file within a second; a conversation
already running finishes on the version it started with, and an edit that
doesn't parse keeps the last good one.

---

## The dialog box

`DialogScreen`: a framed text box along the bottom, a name plate, the
speaker's portrait on the left (the text moves over for it), a bobbing arrow
once the line is all shown, and a box of choices above the text, sized to the
longest one.

- **Moving on:** Confirm (Enter, gamepad South) or a click shows the rest of a
  line, then the next. The key that started the conversation (the
  Interactor's action) does too.
- **Choices:** the arrows / D-pad / mouse move between them, Confirm picks.
- **Typewriter:** each line types out at the player's **Text speed** (Game
  settings: slow, normal, fast, instant), honoring `<pause>` and `<speed>`,
  with a soft blip per character in the speaker's voice (a stock blip for
  speakers without one; narration is silent).
- **Auto-advance** (Game settings): a fully shown line moves on by itself
  after a reading time that grows with its length. Choices always wait.

The settings are `game.text_speed` and `game.auto_advance`
(`DialogSettings`).

**Its look is replaceable.** The layout is `assets/engine/ui/dialog.arcui`
(the widgets `box`, `portrait`, `text`, `next-arrow`, `name-plate`, `name`,
`choices`), and the stock art is `assets/engine/ui/dialog/` - `box.png` (a
nine-slice for the text and choice boxes), `name_plate.png`, `next_arrow.png`,
`cursor.png`, `voice.wav`. Placeholder pixel art for now; replace the files, or
point a screen at your own (`DialogScreen::LayoutPath`, `Art`).

---

## From C++

```
engine/include/arcbit/dialog/
    Dialog.h         the file's data, ParseDialog / LoadDialog and their checks
    DialogLibrary.h  every file in a folder, by id; English into Loc; reloads
    DialogRunner.h   walks a dialog: lines, choices, conditions, actions; no UI
    DialogRequest.h  what Application::StartDialog takes
engine/include/arcbit/ui/DialogScreen.h   the box (and DialogSettings)
engine/src/ApplicationDialog.cpp          the queue, holds, events, Lua wiring
```

`DialogRunner` is the conversation without a screen - give it a dialog and a
`DialogHost` (save data, the event bus, Lua hooks) and drive it with `Next` and
`Choose`. A game wanting a different presentation (a speech bubble over the
NPC) can run one itself, or put a `DialogScreen` of its own
(`Application::CreateDialogScreen`) on the stack.

The tests (`tests/dialog/`, `tests/ui/DialogScreenTests.cpp`,
`tests/script/ScriptDialogTests.cpp`) cover the format, its checks, the runner,
the box and the Lua side, headless.
