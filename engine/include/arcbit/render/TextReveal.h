#pragma once

#include <arcbit/core/Types.h>

#include <functional>

namespace Arcbit
{
struct TextLayout;

// A typewriter: shows a laid-out text a character at a time. It honors the
// text's <pause=s> (waits) and <speed=x> (paces a span), calls OnBlip as
// characters appear - the speaker's voice, at most every BlipGap seconds and
// never for spaces - and OnDone once everything shows. Skip shows the rest
// at once (a press of confirm mid-line).
//
// Draw with TextDrawOptions::Visible = GetVisible(): the character being
// revealed fades in as it comes.
class TextReveal
{
public:
    f32 CharsPerSecond = 40.0f; // at <speed=1>
    f32 BlipGap        = 0.05f; // seconds between voice blips at most

    std::function<void(u32 codepoint)> OnBlip;
    std::function<void()>              OnDone;

    void Start(); // from the first character
    void Update(f32 dt, const TextLayout& layout);
    void Skip(const TextLayout& layout);

    [[nodiscard]] bool IsStarted() const { return _started; }
    [[nodiscard]] bool IsDone() const { return _done; }
    [[nodiscard]] f32  GetVisible() const { return _shown; } // characters shown; fractional while one appears

private:
    void Finish(const TextLayout& layout);
    void CharacterShown(u32 index, const TextLayout& layout);

    f32  _shown     = 0.0f;
    f32  _wait      = 0.0f;   // left of a <pause>
    f32  _sinceBlip = 1e9f;
    u32  _pause     = 0;      // the next of the layout's pauses
    bool _started   = false;
    bool _done      = false;
};
} // namespace Arcbit
