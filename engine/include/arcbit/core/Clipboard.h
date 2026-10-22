#pragma once

#include <string>
#include <string_view>

namespace Arcbit::Clipboard
{
// The system clipboard (UTF-8 text). Without a window - tests, tools - it's
// a store inside the program, so copying and pasting still work there.
void                      SetText(std::string_view text);
[[nodiscard]] std::string GetText();
} // namespace Arcbit::Clipboard
