// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <arcbit/audio/AudioManager.h>
#include <arcbit/audio/MusicPlayer.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Arcbit {
namespace {

ma_engine g_engine;
bool      g_initialized = false;

// The categories, and a sound group for each, nested as they are: a sound's
// gain is its group's times every parent's. Children are freed first.
AudioCategories                                                    g_categories;
std::map<std::string, std::unique_ptr<ma_sound_group>, std::less<>> g_groups;

void ApplyVolume(const AudioCategory& category)
{
    if (const auto it = g_groups.find(category.Name); it != g_groups.end())
        ma_sound_group_set_volume(it->second.get(), AudioManager::SliderToGain(category.Volume));
}

void CreateGroup(const AudioCategory& category)
{
    ma_sound_group* parent = nullptr; // master: straight into the engine's endpoint
    if (const auto it = g_groups.find(category.Parent); it != g_groups.end()) parent = it->second.get();
    auto group = std::make_unique<ma_sound_group>();
    if (ma_sound_group_init(&g_engine, 0, parent, group.get()) != MA_SUCCESS) {
        LOG_ERROR(Engine, "AudioManager: couldn't create category '{}'", category.Name);
        return;
    }
    g_groups[category.Name] = std::move(group);
    ApplyVolume(category);
}

void DestroyGroups()
{
    const auto order = g_categories.InOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if (const auto group = g_groups.find((*it)->Name); group != g_groups.end()) ma_sound_group_uninit(group->second.get());
    g_groups.clear();
}

// A category's group; an unknown name plays in `fallback`, logged once.
ma_sound_group* GroupFor(const std::string_view category, const std::string_view fallback)
{
    if (const auto it = g_groups.find(category); it != g_groups.end()) return it->second.get();
    static std::set<std::string, std::less<>> warned;
    if (warned.emplace(category).second)
        LOG_WARN(Engine, "AudioManager: no audio category '{}' - playing in '{}'", category, fallback);
    const auto it = g_groups.find(fallback);
    return it != g_groups.end() ? it->second.get() : nullptr;
}

// Two music slots, so one track can fade out while the next fades in.
// g_current is the playing one; the other is the outgoing track (or empty),
// freed when the slot is next needed.
struct MusicSlot
{
    ma_sound    Sound{};
    bool        Loaded = false;
    std::string Path;
};
MusicSlot g_music[2];
int       g_current = 0;

void UnloadSlot(MusicSlot& slot)
{
    if (!slot.Loaded) return;
    ma_sound_stop(&slot.Sound);
    ma_sound_uninit(&slot.Sound);
    slot.Loaded = false;
    slot.Path.clear();
}

ma_uint64 Milliseconds(const f32 seconds) { return static_cast<ma_uint64>((std::max)(seconds, 0.0f) * 1000.0f); }

// Fade a slot out; it stays loaded (silent) until the slot is reused.
void FadeOutSlot(MusicSlot& slot, const f32 fadeSeconds)
{
    if (!slot.Loaded) return;
    if (fadeSeconds <= 0.0f) UnloadSlot(slot);
    else ma_sound_stop_with_fade_in_milliseconds(&slot.Sound, Milliseconds(fadeSeconds));
}

// MusicPlayer over the static music controls.
class DeviceMusicPlayer final : public MusicPlayer
{
public:
    void Play(const std::string_view path, const bool loop, const f32 fadeSeconds, const f32 startSeconds,
              const std::string_view category) override
    {
        AudioManager::PlayMusic(path, loop, fadeSeconds, startSeconds, category);
    }
    void        Stop(const f32 fadeSeconds) override { AudioManager::StopMusic(fadeSeconds); }
    std::string GetPath() const override { return AudioManager::GetMusicPath(); }
    f32         GetPosition() const override { return AudioManager::GetMusicPosition(); }
};

// One-shot sounds still playing; finished ones are freed on the next
// PlayOneShot (and all at shutdown).
std::vector<ma_sound*> g_oneShots;

void FreeOneShot(ma_sound* sound)
{
    ma_sound_uninit(sound);
    delete sound;
}

void ReapOneShots()
{
    std::erase_if(g_oneShots, [](ma_sound* s) {
        if (ma_sound_is_playing(s)) return false;
        FreeOneShot(s);
        return true;
    });
}

} // namespace

void AudioManager::Init()
{
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_engine) != MA_SUCCESS)
    {
        LOG_ERROR(Engine, "AudioManager: failed to initialize miniaudio engine");
        return;
    }

    g_initialized = true;
    ApplySettings();
    for (const AudioCategory* category : g_categories.InOrder()) CreateGroup(*category); // parents first

    // Listener direction and world-up for 2D (X right, Z forward = world Y).
    ma_engine_listener_set_direction(&g_engine, 0, 0.0f, 0.0f, -1.0f);
    ma_engine_listener_set_world_up(&g_engine, 0, 0.0f, 1.0f, 0.0f);
    LOG_INFO(Engine, "AudioManager initialized");
}

void AudioManager::Shutdown()
{
    if (!g_initialized) return;

    for (MusicSlot& slot : g_music) UnloadSlot(slot);
    for (ma_sound* sound : g_oneShots) FreeOneShot(sound);
    g_oneShots.clear();

    DestroyGroups();
    ma_engine_uninit(&g_engine);
    g_initialized = false;

    LOG_INFO(Engine, "AudioManager shut down");
}

void AudioManager::PlayOneShot(const std::string_view path, const f32 volume, const std::string_view category)
{
    if (!g_initialized) return;

    // Not ma_engine_play_sound: that has no volume. Decoded up front - one-
    // shots are short - and kept until it finishes.
    ReapOneShots();
    auto* sound = new ma_sound{};
    if (ma_sound_init_from_file(&g_engine, std::string(path).c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                GroupFor(category, AudioCategories::Sfx), nullptr, sound) != MA_SUCCESS) {
        LOG_WARN(Engine, "AudioManager: PlayOneShot failed for '{}'", path);
        delete sound;
        return;
    }
    ma_sound_set_volume(sound, std::clamp(volume, 0.0f, 1.0f));
    ma_sound_start(sound);
    g_oneShots.push_back(sound);
}

void AudioManager::PlayMusic(const std::string_view path, const bool loop, const f32 fadeSeconds,
                             const f32 startSeconds, const std::string_view category)
{
    if (!g_initialized) return;
    // The same track carries on - unless it played to its end (not looping),
    // in which case asking for it again restarts it.
    if (MusicSlot& current = g_music[g_current]; current.Loaded && current.Path == path && !ma_sound_at_end(&current.Sound))
        return;

    // The current track becomes the outgoing one; an older one still fading
    // out in the other slot is cut to make room.
    FadeOutSlot(g_music[g_current], fadeSeconds);
    g_current       = 1 - g_current;
    MusicSlot& next = g_music[g_current];
    UnloadSlot(next);

    // Stream the file and disable spatialization - music plays at flat volume.
    constexpr ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    const std::string   file(path);
    if (ma_sound_init_from_file(&g_engine, file.c_str(), flags, GroupFor(category, AudioCategories::Music), nullptr,
                                &next.Sound) != MA_SUCCESS) {
        LOG_WARN(Engine, "AudioManager: PlayMusic failed for '{}'", path);
        return;
    }
    if (startSeconds > 0.0f) ma_sound_seek_to_second(&next.Sound, startSeconds);
    ma_sound_set_looping(&next.Sound, loop ? MA_TRUE : MA_FALSE);
    if (fadeSeconds > 0.0f) ma_sound_set_fade_in_milliseconds(&next.Sound, 0.0f, 1.0f, Milliseconds(fadeSeconds));
    ma_sound_start(&next.Sound);
    next.Loaded = true;
    next.Path   = file;
    LOG_DEBUG(Engine, "AudioManager: music '{}' (fade {:.1f} s, from {:.1f} s)", path, fadeSeconds, startSeconds);
}

void AudioManager::StopMusic(const f32 fadeSeconds)
{
    if (!g_initialized || !g_music[g_current].Loaded) return;
    LOG_DEBUG(Engine, "AudioManager: music stopped (fade {:.1f} s)", fadeSeconds);
    FadeOutSlot(g_music[g_current], fadeSeconds);
    g_current = 1 - g_current; // the faded track becomes the outgoing one
    UnloadSlot(g_music[g_current]);
}

void AudioManager::PauseMusic()
{
    if (g_initialized && g_music[g_current].Loaded) ma_sound_stop(&g_music[g_current].Sound);
}

void AudioManager::ResumeMusic()
{
    if (g_initialized && g_music[g_current].Loaded) ma_sound_start(&g_music[g_current].Sound);
}

std::string AudioManager::GetMusicPath()
{
    return g_initialized && g_music[g_current].Loaded ? g_music[g_current].Path : std::string{};
}

f32 AudioManager::GetMusicPosition()
{
    f32 seconds = 0.0f;
    if (g_initialized && g_music[g_current].Loaded) ma_sound_get_cursor_in_seconds(&g_music[g_current].Sound, &seconds);
    return seconds;
}

f32 AudioManager::SliderToGain(const f32 slider)
{
    if (slider <= 0.0f) return 0.0f;
    return std::pow((std::min)(slider, 1.0f), 1.0f / 0.6f);
}

MusicPlayer& AudioManager::GetMusicPlayer()
{
    static DeviceMusicPlayer player;
    return player;
}

bool AudioManager::DefineCategory(const std::string_view name, const std::string_view parent, const std::string_view label)
{
    if (!g_categories.Define(name, parent, label)) return false;
    g_categories.SetVolume(name, Settings::Audio.Volume(name));
    if (g_initialized) CreateGroup(*g_categories.Find(name));
    return true;
}

const AudioCategories& AudioManager::GetCategories() { return g_categories; }

void AudioManager::SetVolume(const std::string_view category, const f32 volume)
{
    g_categories.SetVolume(category, volume);
    if (const AudioCategory* c = g_categories.Find(category); c && g_initialized) ApplyVolume(*c);
}

f32 AudioManager::GetVolume(const std::string_view category) { return g_categories.GetVolume(category); }

void AudioManager::ApplySettings()
{
    for (const AudioCategory& category : g_categories.All()) SetVolume(category.Name, Settings::Audio.Volume(category.Name));
}

void AudioManager::SetListenerPosition(Vec2 position)
{
    if (!g_initialized) return;
    // Map 2D world (X, Y) → audio (X, 0, Y) so that left/right and up/down
    // distances both contribute to attenuation in a top-down view.
    ma_engine_listener_set_position(&g_engine, 0, position.X, 0.0f, position.Y);
}

void* AudioManager::CreateSpatialSound(const std::string_view path, const bool loop, const f32 volume,
                                       const std::string_view category)
{
    if (!g_initialized) return nullptr;

    auto* sound = new ma_sound{};

    // Fully decode for minimal latency on position updates.
    constexpr ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    const ma_result result = ma_sound_init_from_file(
        &g_engine, std::string(path).c_str(), flags, GroupFor(category, AudioCategories::Sfx), nullptr, sound);

    if (result != MA_SUCCESS)
    {
        LOG_WARN(Engine, "AudioManager: CreateSpatialSound failed for '{}'", path);
        delete sound;
        return nullptr;
    }

    ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound, volume);
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_start(sound);
    return sound;
}

void AudioManager::SetSpatialSoundPosition(void* handle, Vec2 position, f32 radius)
{
    if (!handle) return;
    auto* sound = static_cast<ma_sound*>(handle);
    ma_sound_set_position(sound, position.X, 0.0f, position.Y);
    // Full volume within 10 % of the radius; silent beyond the radius.
    ma_sound_set_min_distance(sound, radius * 0.1f);
    ma_sound_set_max_distance(sound, radius);
}

void AudioManager::DestroySpatialSound(void* handle)
{
    if (!handle) return;
    auto* sound = static_cast<ma_sound*>(handle);
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    delete sound;
}

} // namespace Arcbit
