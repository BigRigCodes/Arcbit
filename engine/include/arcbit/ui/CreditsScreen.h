#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/render/RenderHandle.h>
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
//           { "heading": "Libraries", "names": ["SDL3", "Vulkan"] },
//           { "heading": "Built with", "names": [
//               { "image": "assets/logos/arcbit.png", "height": 160 },
//               { "images": ["assets/logos/sdl.png", "assets/logos/vulkan.png"], "height": 64 } ] }
//       ]
//   }
//
// A name can be a logo: "image" (a path), drawn "height" pixels tall (default
// 96) at its own aspect, optionally cropped to "uv" [u0, v0, u1, v1]; or
// "images", a row of logos side by side at one height.
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
            Images, // a logo, or a row of them
        };
        std::string              Text;
        Kind                     Style = Kind::Name;
        f32                      Y     = 0.0f; // top of the line, from the top of the roll
        std::vector<std::string> Images;       // Images: their paths
        f32                      ImageHeight = 0.0f;
        UVRect                   UV;           // Images: the part of each image shown
    };

    // A logo's texture and its size in pixels (TextureInfo); Application
    // wires its texture manager. Unset or invalid, the logo is left out.
    struct LoadedImage
    {
        TextureHandle Texture;
        Vec2          Size;
    };

    std::string                                         Path;
    f32                                                 ViewHeight = 1080.0f;
    std::function<void()>                               OnClose;
    std::function<LoadedImage(const std::string& path)> LoadImage;
    SamplerHandle                                       ImageSampler;

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
    struct Placed
    {
        UIWidget* Widget;
        Vec2      At; // X from the center, Y in the roll
    };
    std::vector<Placed>                 _placed;
    f32                                 _height = 0.0f;
    f32                                 _scroll = 0.0f;
    f32                                 _speed  = 50.0f;
    bool                                _closed = false;

    void Close();
    void AddLabel(const Line& line);
    void AddImages(const Line& line);
    void Place(); // offsets from the bottom edge, scrolled up by _scroll
};
} // namespace Arcbit
