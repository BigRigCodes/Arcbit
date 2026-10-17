#pragma once

#include <arcbit/ui/UIScreen.h>

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class Label;

// ---------------------------------------------------------------------------
// CreditsScreen - scrolling credits read from a JSON file.
//
//   {
//       "title": "Arcbit Demos",
//       "scroll_speed": 50,
//       "sections": [
//           { "heading": "Engine", "names": ["BigRigCodes"] },
//           { "heading": "Libraries", "names": ["SDL3", "Vulkan"] }
//       ]
//   }
//
// The roll starts below the screen and scrolls up at scroll_speed px/s
// (default 50); when the last line has left the top, or on back (Escape /
// gamepad East), OnClose fires. Set ViewHeight to the window height before
// pushing so the roll knows where the screen ends.
// ---------------------------------------------------------------------------
class CreditsScreen : public UIScreen
{
public:
    // One laid-out line of the roll.
    struct Line
    {
        enum class Kind : u8
        {
            Title,
            Heading,
            Name,
        };
        std::string Text;
        Kind        Style = Kind::Name;
        f32         Y     = 0.0f; // top of the line, from the top of the roll
    };

    std::string           Path;
    f32                   ViewHeight = 1080.0f;
    std::function<void()> OnClose;

    CreditsScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 4.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override;
    void OnTick(f32 dt, const InputManager& input) override;

    // Lay out a credits document: the title, then each section's heading and
    // names, with a gap between sections. Returns the lines and the roll's
    // total height. Missing keys are fine; a non-object yields nothing.
    [[nodiscard]] static std::vector<Line> Layout(const nlohmann::json& doc, f32& height);

private:
    std::vector<std::pair<Label*, f32>> _labels; // label, its Y in the roll
    f32                                 _height = 0.0f;
    f32                                 _scroll = 0.0f;
    f32                                 _speed  = 50.0f;
    bool                                _closed = false;

    void Close();
    void PlaceLabels(); // offsets from the bottom edge, scrolled up by _scroll
};
} // namespace Arcbit
