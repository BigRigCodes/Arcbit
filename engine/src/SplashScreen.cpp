#include <arcbit/ui/SplashScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/input/InputManager.h>

#include <algorithm>

namespace Arcbit
{

void SplashScreen::OnEnter()
{
    _roots.clear();
    _currentIndex = 0;
    _entryTimer   = 0.0f;
    _done         = false;

    auto* img        = Add<Image>();
    img->SizePercent = {1.0f, 1.0f};
    img->Anchor      = {0.0f, 0.0f};
    img->Pivot       = {0.0f, 0.0f};
    img->ZOrder      = 1;
    _image = img;

    if (Entries.empty()) {
        Complete();
        return;
    }
    ShowEntry(0);
}

void SplashScreen::ShowEntry(const i32 idx)
{
    if (!_image || idx >= static_cast<i32>(Entries.size())) return;
    _image->Texture = Entries[idx].Texture;
    _image->Sampler = Entries[idx].Sampler;
    _image->Visible = _image->Texture.IsValid();
    _image->Opacity = 0.0f;
    _entryTimer     = 0.0f;
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
    if (_image) _image->Opacity = std::max(0.0f, ComputeOpacity());

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
    // Raise TransitionSpeed so Pop() starts a fade-out instead of immediately
    // removing the screen mid-OnTick (which would leave a dangling pointer in Update).
    TransitionSpeed = 10000.0f;
    if (OnComplete) OnComplete();
}

} // namespace Arcbit
