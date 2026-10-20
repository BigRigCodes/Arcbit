#include <arcbit/audio/AudioCategories.h>

#include <arcbit/core/Log.h>

#include <algorithm>

namespace Arcbit
{
AudioCategories::AudioCategories()
{
    _categories.push_back({ .Name = std::string(Master), .Label = "ui.audio.master" });
    Define(Music, Master, "ui.audio.music");
    Define(Sfx, Master, "ui.audio.sfx");
    Define(Voice, Master, "ui.audio.voice");
    Define(Ui, Master, "ui.audio.ui");
}

bool AudioCategories::Define(const std::string_view name, const std::string_view parent, const std::string_view label)
{
    const AudioCategory* up = Find(parent);
    if (name.empty() || Has(name) || !up) {
        LOG_WARN(Engine, "Audio: can't define category '{}' under '{}' - {}", name, parent,
                 !up ? "no such parent" : name.empty() ? "it needs a name" : "it exists");
        return false;
    }
    _categories.push_back({ .Name   = std::string(name),
                            .Parent = std::string(parent),
                            .Label  = label.empty() ? std::string(name) : std::string(label),
                            .Depth  = up->Depth + 1 });
    return true;
}

const AudioCategory* AudioCategories::Find(const std::string_view name) const
{
    const auto it = std::ranges::find(_categories, name, &AudioCategory::Name);
    return it != _categories.end() ? &*it : nullptr;
}

void AudioCategories::SetVolume(const std::string_view name, const f32 volume)
{
    const auto it = std::ranges::find(_categories, name, &AudioCategory::Name);
    if (it != _categories.end()) it->Volume = std::clamp(volume, 0.0f, 1.0f);
}

f32 AudioCategories::GetVolume(const std::string_view name) const
{
    const AudioCategory* category = Find(name);
    return category ? category->Volume : 1.0f;
}

f32 AudioCategories::EffectiveVolume(const std::string_view name) const
{
    f32 volume = 1.0f;
    for (const AudioCategory* c = Find(name); c; c = c->Parent.empty() ? nullptr : Find(c->Parent)) volume *= c->Volume;
    return volume;
}

std::vector<const AudioCategory*> AudioCategories::InOrder() const
{
    std::vector<const AudioCategory*> out{ &_categories.front() };
    AddChildren(Master, out);
    return out;
}

void AudioCategories::AddChildren(const std::string_view parent, std::vector<const AudioCategory*>& out) const
{
    for (const AudioCategory& c : _categories) {
        if (c.Parent != parent) continue;
        out.push_back(&c);
        AddChildren(c.Name, out);
    }
}
} // namespace Arcbit
