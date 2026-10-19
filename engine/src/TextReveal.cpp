#include <arcbit/render/TextReveal.h>
#include <arcbit/render/RichText.h>

#include <algorithm>
#include <cmath>

namespace Arcbit
{
void TextReveal::Start()
{
    _shown = _wait = 0.0f;
    _sinceBlip = 1e9f;
    _pause     = 0;
    _started   = true;
    _done      = false;
}

// Steps one character boundary at a time, so a pause is never stepped over
// and each character's own <speed> paces it.
void TextReveal::Update(f32 dt, const TextLayout& layout)
{
    if (!_started || _done) return;
    _sinceBlip += dt;
    while (dt > 0.0f) {
        const u32 shown = static_cast<u32>(_shown);
        if (shown >= layout.CharCount) return Finish(layout);
        if (_wait > 0.0f) {
            const f32 waited = std::min(_wait, dt);
            _wait -= waited, dt -= waited;
            continue;
        }
        if (_pause < layout.Pauses.size() && layout.Pauses[_pause].Index <= shown && _shown == static_cast<f32>(shown)) {
            _wait = layout.Pauses[_pause++].Seconds;
            continue;
        }
        const f32 pace = std::max(0.01f, layout.Spans[layout.CharSpan[shown]].Style.Speed) * CharsPerSecond;
        const f32 left = static_cast<f32>(shown + 1) - _shown; // of this character
        if (dt * pace < left) {
            _shown += dt * pace;
            return;
        }
        dt    -= left / pace;
        _shown = static_cast<f32>(shown + 1);
        CharacterShown(shown, layout);
    }
    if (static_cast<u32>(_shown) >= layout.CharCount) Finish(layout);
}

void TextReveal::Skip(const TextLayout& layout)
{
    if (!_started || _done) return;
    _shown = static_cast<f32>(layout.CharCount);
    Finish(layout);
}

void TextReveal::Finish(const TextLayout& layout)
{
    _shown = static_cast<f32>(layout.CharCount);
    _done  = true;
    if (OnDone) OnDone();
}

void TextReveal::CharacterShown(const u32 index, const TextLayout& layout)
{
    const u32 cp = layout.Codepoints[index];
    if (cp == ' ' || cp == '\n' || cp == '\t' || cp == 0x3000 || _sinceBlip < BlipGap) return;
    _sinceBlip = 0.0f;
    if (OnBlip) OnBlip(cp);
}
} // namespace Arcbit
