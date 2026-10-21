# Audio

`AudioManager` plays sound through miniaudio: one-shot sounds, background
music, and spatial sounds that fade with distance from the camera. Every
sound plays in an **audio category**, and every category has a volume the
player sets.

- [Categories](#categories)
- [Playing sounds](#playing-sounds)
- [Music](#music)
- [Volume](#volume)

---

## Categories

```
master
├── music        Music Volume      scene music
├── sfx          SFX Volume        one-shots, AudioSource
├── voice        Voice Volume      dialog blips
└── ui           Menu Volume       menu sounds (the UI skin's)
```

The engine's five: `master`, and `music`, `sfx`, `voice` and `ui` side by
side under it - muting SFX doesn't mute voices or menus. A game adds its own,
under any category, as deep as it likes:

```cpp
void MyGame::OnStart()
{
    AudioManager::DefineCategory("music.menu", "music", "audio.menu_music");   // parent, label (Loc key or text)
    AudioManager::DefineCategory("music.game", "music", "audio.game_music");
    AudioManager::DefineCategory("ambience");                                    // under master, labelled "ambience"
}
```

A sound in a category is heard at its category's volume times every
parent's: at master 80%, music 50% and music.game 50%, game music plays at
20%. (Each category is a miniaudio sound group inside its parent's, so the
mixer does the multiplying.)

The demo defines `sfx.footsteps` for the World demo's footsteps.

---

## Playing sounds

```cpp
AudioManager::PlayOneShot("assets/sfx/chest.wav");                    // sfx
AudioManager::PlayOneShot("assets/sfx/coin.wav", 0.6f, "ui");          // gain 0.6, in ui
```

The `volume` argument is plain gain - the author's mix - under the
category's volume. A sound in a category that doesn't exist plays in `sfx`
(music in `music`), logged once.

**Spatial sounds** are `AudioSource` components: a path, a gain, a hearing
radius, looping, and `Category` (default `"sfx"`; `"category"` in scene
files).

**Engine sounds** pick theirs: the UI skin's sounds (focus, click, back,
sliders) play in `ui`, dialog voice blips in `voice`.

**Lua**: `Audio.PlaySound(path, volume, category)`; volume and category are
optional.

---

## Music

One track plays at a time. `Scene::SetMusic(path, loop, category)` - or the
scene file's `"music": { "path", "loop", "category" }`, or Lua's
`Audio.PlayMusic(path, loop, category)` - gives a scene its track, and the
music follows the active scene, crossfading at each swap
([scenes.md](scenes.md#music)). The category (default `"music"`) is the
volume it plays at: a title menu's track in `music.menu` and the game's in
`music.game` each follow their own slider, and a crossfade from one to the
other fades each at its own.

---

## Volume

The player's volumes are saved by category name with their settings
(`Settings::Audio.Volumes`; `"audio": { "volumes": { "music": 0.4 } }` in
`settings.json`). A category with no saved volume is at 100%. A settings file
from before categories (`master_volume`, `music_volume`, `sfx_volume`) reads
into `master`, `music` and `sfx`.

Volumes are perceived loudness, not gain: 50% sounds about half as loud
(-10 dB), 10% about a tenth (`AudioManager::SliderToGain`).

The **audio settings screen** (`AudioSettingsScreen`) has a row for every
category the game has, in tree order, a game's own indented under its
parent. Each row is the layout's `volume-row` template
(`assets/engine/ui/audio_settings.arcui`), so a game restyles one row and
gets them all. Changes apply as the player makes them.

`AudioManager::SetVolume(category, v)` sets a volume for this run only (the
demo's `--mute` sets master to 0); the settings screen is what saves.
