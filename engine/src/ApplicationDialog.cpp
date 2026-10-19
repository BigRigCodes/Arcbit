// Application: conversations - the dialog box and what it's drawn with
// (docs/dialog.md).

#include <arcbit/app/Application.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Log.h>

#include <format>

namespace Arcbit
{
std::unique_ptr<DialogScreen> Application::CreateDialogScreen()
{
    if (!_dialogArt.Sampler.IsValid()) {
        const auto load = [this](const std::string_view name) {
            const std::string   path    = std::format("assets/engine/ui/dialog/{}.png", name);
            const TextureHandle texture = _textures->Load(path);
            if (!texture.IsValid()) LOG_WARN(Engine, "Dialog art '{}' not found", path);
            return texture;
        };
        _dialogArt = { .Box       = load("box"),
                       .NamePlate = load("name_plate"),
                       .NextArrow = load("next_arrow"),
                       .Cursor    = load("cursor"),
                       .Sampler   = _nearestSampler };
    }
    auto screen          = std::make_unique<DialogScreen>();
    screen->Art          = _dialogArt;
    screen->LoadPortrait = [this](const std::string& path) { return _textures->Load(path); };
    screen->PlayVoice    = [](const std::string& path) { AudioManager::PlayOneShot(path); };
    screen->DefaultVoice = "assets/engine/ui/dialog/voice.wav";
    screen->Skin         = &_ui.GetSkin();
    return screen;
}
} // namespace Arcbit
