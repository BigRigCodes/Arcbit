#pragma once

#include <arcbit/core/Types.h>

#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Audio categories (docs/audio.md): every sound plays in one, and each has
// a volume the player sets. They nest - a category's sounds are scaled by
// its own volume and every parent's up to "master" - so a game can split
// "music" into "music.combat" or add "ambience" beside "sfx".
//
// The engine's: master, and directly under it music, sfx, voice (dialog
// blips) and ui (menu sounds) - muting SFX doesn't mute voices or menus.
// AudioManager plays through this tree; it's kept apart from miniaudio so
// it can be tested on its own.
// ---------------------------------------------------------------------------
struct AudioCategory
{
    std::string Name;   // "music.combat"
    std::string Parent; // empty only for master
    std::string Label;  // its name on the audio settings screen: a Loc key or text
    f32         Volume = 1.0f; // the player's slider, 0..1 (perceived loudness)
    u32         Depth  = 0;    // levels below master
};

class AudioCategories
{
public:
    static constexpr std::string_view Master = "master";
    static constexpr std::string_view Music  = "music";
    static constexpr std::string_view Sfx    = "sfx";
    static constexpr std::string_view Voice  = "voice";
    static constexpr std::string_view Ui     = "ui";

    AudioCategories(); // master and the engine's four

    // A new category under parent (default master). False, logged, if the
    // name is taken or empty or the parent unknown.
    bool Define(std::string_view name, std::string_view parent = Master, std::string_view label = {});

    [[nodiscard]] const AudioCategory* Find(std::string_view name) const;
    [[nodiscard]] bool                 Has(std::string_view name) const { return Find(name) != nullptr; }

    // The player's slider for one category; unknown names are ignored / read 1.
    void              SetVolume(std::string_view name, f32 volume);
    [[nodiscard]] f32 GetVolume(std::string_view name) const;

    // The slider a sound in this category is heard at: its own times every
    // parent's (AudioManager::SliderToGain turns it into gain).
    [[nodiscard]] f32 EffectiveVolume(std::string_view name) const;

    // Every category, parents before their children, each category's
    // children in the order defined - the settings screen's order.
    [[nodiscard]] std::vector<const AudioCategory*> InOrder() const;

    // In definition order (master first).
    [[nodiscard]] const std::vector<AudioCategory>& All() const { return _categories; }

private:
    std::vector<AudioCategory> _categories;

    void AddChildren(std::string_view parent, std::vector<const AudioCategory*>& out) const;
};
} // namespace Arcbit
