#pragma once

#include <arcbit/audio/AudioCategories.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

#include <string>
#include <string_view>

namespace Arcbit {

class MusicPlayer;

// ---------------------------------------------------------------------------
// AudioManager
//
// Static facade over miniaudio. Handles device init/shutdown, background music
// streaming, fire-and-forget SFX, and spatial point sounds (driven by AudioSystem).
//
// Every sound plays in an audio category (AudioCategories.h): one-shots
// and spatial sounds in "sfx" unless told otherwise, music in "music". Each
// category is a miniaudio sound group under its parent's, so a sound is
// heard at its category's volume times every parent's up to "master".
//
// Those volumes are perceived loudness, not amplitude (SliderToGain): ears
// hear amplitude on a log scale, so a linear 10% slider (-20 dB) still sounds
// about a quarter as loud. The per-sound `volume` arguments below are plain
// gain - the author's mix - applied on top.
//
// Call Init() at startup (after Settings::Init) and Shutdown() before the
// program exits. All other methods are safe to call from game code.
// ---------------------------------------------------------------------------
class AudioManager
{
public:
    // Call once at startup, after Settings::Init(). Reads initial volumes from
    // Settings::Audio automatically.
    static void Init();

    // Stop all playback and release the audio device.
    static void Shutdown();

    // -----------------------------------------------------------------------
    // SFX — fire-and-forget
    // -----------------------------------------------------------------------

    // Play a short sound at the given gain [0, 1] in a category (an unknown
    // one plays in sfx, logged). Fire-and-forget: freed once it finishes.
    static void PlayOneShot(std::string_view path, f32 volume = 1.0f, std::string_view category = AudioCategories::Sfx);

    // -----------------------------------------------------------------------
    // Music
    // -----------------------------------------------------------------------

    // Stream a music file, crossfading from the current track over
    // fadeSeconds (0 = cut) and starting startSeconds in. A no-op when `path`
    // is already the current track: it carries on. Scenes don't call this
    // directly - Scene::SetMusic hands the track to the scene manager. One
    // track plays at a time, whatever its category: the category is the
    // volume it plays at ("music", or a game's "music.combat").
    static void PlayMusic(std::string_view path, bool loop = true, f32 fadeSeconds = 0.0f, f32 startSeconds = 0.0f,
                          std::string_view category = AudioCategories::Music);

    // Fade the current music out over fadeSeconds (0 = stop at once). No-op
    // if nothing is playing.
    static void StopMusic(f32 fadeSeconds = 0.0f);

    static void PauseMusic();
    static void ResumeMusic();

    // The current track's path (empty when none) and how far into it it is.
    [[nodiscard]] static std::string GetMusicPath();
    [[nodiscard]] static f32         GetMusicPosition();

    // The music controls as a MusicPlayer, for SceneManager.
    [[nodiscard]] static MusicPlayer& GetMusicPlayer();

    // -----------------------------------------------------------------------
    // Categories and volume
    // -----------------------------------------------------------------------

    // A game's own category, under parent (default master), with its name
    // on the audio settings screen (a Loc key or text; default the name).
    // Takes the player's saved volume for it. Any time, before or after Init.
    static bool DefineCategory(std::string_view name, std::string_view parent = AudioCategories::Master,
                               std::string_view label = {});

    [[nodiscard]] static const AudioCategories& GetCategories();

    // A category's volume (the player's slider, 0..1), applied at once. Not
    // saved - the audio settings screen saves it in Settings::Audio.
    static void              SetVolume(std::string_view category, f32 volume);
    [[nodiscard]] static f32 GetVolume(std::string_view category);

    // Every category's volume from Settings::Audio.
    static void ApplySettings();

    // A volume setting (perceived loudness, 0..1) as linear gain. Perceived
    // loudness goes as gain^0.6 (Stevens' power law), so gain =
    // slider^(1/0.6): 1 -> 1 (as authored), 0.5 -> 0.31 (-10 dB, "half as
    // loud"), 0.1 -> 0.02 (-33 dB), 0 -> silence.
    [[nodiscard]] static f32 SliderToGain(f32 slider);

    // -----------------------------------------------------------------------
    // Spatial sounds — called by AudioSystem
    // -----------------------------------------------------------------------

    // Move the audio listener to the given world position each tick.
    // AudioSystem calls this automatically with the camera position.
    static void SetListenerPosition(Vec2 position);

    // Create a looping or one-shot spatial sound at the given world position.
    // Returns an opaque handle; caller must call DestroySpatialSound when done.
    // Returns nullptr if the file cannot be opened.
    [[nodiscard]] static void* CreateSpatialSound(std::string_view path, bool loop, f32 volume,
                                                  std::string_view category = AudioCategories::Sfx);

    // Update position and max-hearing radius each tick.
    static void SetSpatialSoundPosition(void* handle, Vec2 position, f32 radius);

    // Stop playback and free the handle returned by CreateSpatialSound.
    static void DestroySpatialSound(void* handle);
};

} // namespace Arcbit
