#include <arcbit/core/Clipboard.h>

#include <SDL3/SDL.h>

namespace Arcbit::Clipboard
{
namespace
{
std::string& Local()
{
    static std::string text; // without SDL's video (no window), the clipboard lives here
    return text;
}
} // namespace

void SetText(const std::string_view text)
{
    Local() = std::string(text);
    (void)SDL_SetClipboardText(Local().c_str()); // fails quietly without a window
}

std::string GetText()
{
    if (!SDL_HasClipboardText()) return Local();
    char*             raw = SDL_GetClipboardText();
    const std::string text = raw ? raw : "";
    SDL_free(raw);
    return text;
}
} // namespace Arcbit::Clipboard
