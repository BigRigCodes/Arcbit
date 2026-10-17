#include <arcbit/ui/SplashScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/input/InputManager.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Arcbit
{

void SplashScreen::OnEnter()
{
    _roots.clear();
    _currentIndex = 0;
    _entryTimer   = 0.0f;
    _done         = false;

    auto* black                    = Add<Scrim>();
    black->SkinOverride.ScrimColor = Color{ 0.0f, 0.0f, 0.0f, 1.0f };

    auto* img        = Add<Image>();
    img->SizePercent = {1.0f, 1.0f};
    img->Anchor      = {0.0f, 0.0f};
    img->Pivot       = {0.0f, 0.0f};
    img->ZOrder      = 1;
    _image = img;

    _icon         = Add<Image>();
    _icon->Size   = {88.0f, 88.0f};
    _icon->Anchor = {0.5f, 0.5f};
    _icon->Pivot  = {0.5f, 1.0f};
    _icon->Offset = {0.0f, -24.0f};
    _icon->ZOrder = 1;

    _text                          = Add<Label>();
    _text->Align                   = TextAlign::Center;
    _text->WordWrap                = true;
    _text->Size                    = {760.0f, 140.0f};
    _text->Anchor                  = {0.5f, 0.5f};
    _text->Pivot                   = {0.5f, 0.0f};
    _text->Offset                  = {0.0f, 8.0f};
    _text->ZOrder                  = 1;
    _text->SkinOverride.TextLabel  = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    _text->SkinOverride.FontScale  = 1.15f;

    if (Entries.empty()) {
        Complete();
        return;
    }
    ShowEntry(0);
}

void SplashScreen::ShowEntry(const i32 idx)
{
    if (!_image || idx >= static_cast<i32>(Entries.size())) return;
    const SplashEntry& e = Entries[idx];
    _image->Texture      = e.Texture;
    _image->Sampler      = e.Sampler;
    _image->Visible      = e.Texture.IsValid();
    _icon->Texture       = e.Icon;
    _icon->Sampler       = e.IconSampler;
    _icon->Tint          = e.IconTint;
    _icon->Rotation      = 0.0f;
    _icon->Visible       = e.Icon.IsValid();
    _text->Text          = e.Text;
    _text->Visible       = !e.Text.empty();
    SetOpacity(0.0f);
    _entryTimer = 0.0f;
    if (e.OnShown) e.OnShown();
}

void SplashScreen::SetOpacity(const f32 opacity)
{
    _image->Opacity = opacity;
    _icon->Opacity  = opacity;
    _text->Opacity  = opacity;
}

f32 SplashScreen::ComputeOpacity() const
{
    if (_currentIndex >= static_cast<i32>(Entries.size())) return 0.0f;
    const SplashEntry& e = Entries[_currentIndex];
    if (_entryTimer < e.FadeIn)
        return _entryTimer / e.FadeIn;
    if (_entryTimer < e.Duration - e.FadeOut)
        return 1.0f;
    const f32 fadeOutStart = e.Duration - e.FadeOut;
    return e.FadeOut > 0.0f ? 1.0f - (_entryTimer - fadeOutStart) / e.FadeOut : 0.0f;
}

void SplashScreen::OnTick(const f32 dt, const InputManager& input)
{
    if (_done || Entries.empty()) return;

    if (input.JustPressed(UIManager::ActionConfirm)) {
        Complete();
        return;
    }

    _entryTimer += dt;
    if (_image) SetOpacity(std::max(0.0f, ComputeOpacity()));
    if (_icon && Entries[_currentIndex].SpinIcon) {
        constexpr f32 Turn = 2.0f * std::numbers::pi_v<f32>;
        _icon->Rotation    = std::fmod(_icon->Rotation + dt * 0.8f * Turn, Turn); // the indicator's speed
    }

    if (_entryTimer >= Entries[_currentIndex].Duration) {
        ++_currentIndex;
        if (_currentIndex >= static_cast<i32>(Entries.size()))
            Complete();
        else
            ShowEntry(_currentIndex);
    }
}

bool SplashScreen::CoversLoading() const
{
    if (_done || _currentIndex >= static_cast<i32>(Entries.size())) return false;
    const SplashEntry& e = Entries[_currentIndex];
    return _entryTimer >= e.FadeIn && _entryTimer <= e.Duration - e.FadeOut;
}

void SplashScreen::OnBackPressed() { Complete(); }

void SplashScreen::Skip() { Complete(); }

void SplashScreen::Complete()
{
    if (_done) return;
    _done = true;
    if (_image) _image->Visible = false;
    if (_icon) _icon->Visible = false;
    if (_text) _text->Visible = false;
    // Raise TransitionSpeed so Pop() starts a fade-out instead of immediately
    // removing the screen mid-OnTick (which would leave a dangling pointer in Update).
    TransitionSpeed = 10000.0f;
    if (OnComplete) OnComplete();
}

} // namespace Arcbit
