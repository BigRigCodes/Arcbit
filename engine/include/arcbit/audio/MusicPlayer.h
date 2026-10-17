#pragma once

#include <arcbit/core/Types.h>

#include <string>
#include <string_view>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// MusicPlayer - the background-music controls SceneManager drives, so scene
// music works the same against the real device (AudioManager::GetMusicPlayer)
// and a recording stand-in in headless tests.
// ---------------------------------------------------------------------------
class MusicPlayer
{
public:
    virtual ~MusicPlayer() = default;

    // Start `path`, crossfading from whatever plays over fadeSeconds and
    // starting startSeconds into the track. A no-op when `path` is already
    // the current track: it carries on uninterrupted.
    virtual void Play(std::string_view path, bool loop, f32 fadeSeconds, f32 startSeconds) = 0;

    // Fade the current track out over fadeSeconds (0 = stop at once).
    virtual void Stop(f32 fadeSeconds) = 0;

    // The current track's path; empty when nothing plays.
    [[nodiscard]] virtual std::string GetPath() const = 0;

    // Seconds into the current track.
    [[nodiscard]] virtual f32 GetPosition() const = 0;
};
} // namespace Arcbit
