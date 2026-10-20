# Input

Physical inputs - keys, mouse buttons, gamepad buttons and stick axes - map to
named **actions**; game code asks about actions, never keys. The player's
bindings are saved with their settings and survive game updates.

- [Actions and bindings](#actions-and-bindings)
- [Saved bindings](#saved-bindings)
- [Across game updates](#across-game-updates)

---

## Actions and bindings

```cpp
constexpr ActionID Jump = MakeAction("Jump");

void MyGame::OnStart()
{
    GetInput().RegisterAction(Jump, "Jump", "input.jump", "input.category.movement");
    GetInput().BindKey(Jump, Key::Space);
    GetInput().BindGamepadButton(Jump, GamepadButton::South);
}

if (GetInput().JustPressed(Jump)) StartJump();
```

`RegisterAction` names the action (the name it's saved under), gives it a
display name and a category for the Controls screen (either may be a Loc
key), and the bindings set in `OnStart` are its **defaults**. An action can
have any number of bindings; it's pressed while any of them is. A stick axis
binds whole or by half (`AxisHalf::Negative` / `Positive`), so one stick can
drive Move_Left and Move_Right.

The UI's own actions (`UI_*`: confirm, back, navigation, text editing) are
the engine's and always keep their bindings; rebinding a game action never
takes them away. The Controls screen (`InputRebindScreen`) rebinds the rest.

---

## Saved bindings

Application loads the player's bindings right after `OnStart`
(`Settings::LoadInputBindings`) and saves them at shutdown
(`SaveInputBindings`), in the `input` section of `settings.json` in the
player's data folder ([saves.md](saves.md#slots-and-files)):

```json
"input": {
    "bindings": { "Interact": [ { "type": "Key", "key": "E" }, { "type": "GamepadButton", "button": "South" } ] },
    "defaults": { "Interact": [ { "type": "Key", "key": "F" }, { "type": "GamepadButton", "button": "South" } ] }
}
```

`bindings` are the player's; `defaults` are the game's defaults as of the
last launch - what the next one compares against to see what an update
changed. A file without `defaults` (written before they were recorded) is
read as all the player's.

---

## Across game updates

A game update may change an action's default, add actions, or both. The
player's keys always win (`BindingUpdate.h`, `MergeBindings`):

- **An action the player had keeps its bindings** - rebound or not - whatever
  its default is now. Changing a default only changes it for new players.
- **A new action takes each default binding that's free.** One that another
  action holds goes to *that* action's own default instead, when that action
  isn't on it - the update moved its default, or the player rebound it - and
  it's free:

  | | Interact | Melee |
  |---|---|---|
  | The release | E | - |
  | The update's defaults | F | E |
  | The player gets | E (kept) | F (freed by Interact) |

  The same if the player had moved Interact to F themselves and the update
  added Melee on F: Melee gets E, the key Interact left. Otherwise that
  binding stays unbound. Keys and mouse buttons are placed among themselves,
  gamepad buttons and axes among themselves.
- **A forced reset.** An old default that was broken can be put right:
  `Settings::ResetBindingOnUpdate("Interact")` in `OnStart` places the
  action like a new one, through the same rules - once, on the first launch
  whose recorded default differs. If none of its new defaults can go
  anywhere, it keeps its old bindings.

**The notice.** Once the boot is done, the player is told once about the
actions that didn't get their default keys, and resets. A new action that
simply landed on its defaults isn't announced (a project can list those too:
`Defaults.BindingNoticeAllNew`).

```
Controls updated
New: Melee is F (E is your Interact)
Dash has no key yet - set it in Controls (Shift is your Sprint)
```

By default it's a card that waits for OK (`NoticeScreen`), over the title
menu. A project can make it a toast that fades by itself (`NoticeToast`) or
show nothing, with `ApplicationConfig::Defaults.BindingNotice`
(`NoticeStyle::Card` / `Toast` / `None`). Either way the engine publishes
`engine.input.bindings_updated` (`count`, `text`: the same lines) for a game that
tells the player its own way. The lines are localized
(`ui.bindings_notice.*`); `Settings::GetBindingChanges` has the changes
themselves.
