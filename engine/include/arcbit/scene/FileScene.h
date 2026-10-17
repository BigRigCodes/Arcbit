#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneFile.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// FileScene - a scene built entirely from an .arcscene file (docs/scenes.md).
//
// The data-driven path: register it like any scene and the editor-made game
// needs no C++ scene class.
//
//   scenes.Register("Cave", [] { return std::make_unique<FileScene>("assets/scenes/cave.arcscene"); });
//
// OnLoad reads the file through the application's SceneRegistry and assets.
// Per frame it applies the file's ambient light and, when physics debug draw
// is on, draws collider outlines. Textures and sprite sheets it loads are
// owned by the scene and released when it unloads.
// ---------------------------------------------------------------------------
class FileScene : public Scene
{
public:
    explicit FileScene(std::string path) : _path(std::move(path)) {}

    void OnLoad() override;
    void OnRender(FramePacket& packet) override;

    // True once OnLoad has built the file without errors.
    [[nodiscard]] bool IsLoaded() const { return _loaded; }

    // The runtime entity for a file entity id; invalid if there's none.
    [[nodiscard]] Entity FindEntity(std::string_view id) const;

    // Write the scene's current state to `path` (default: the file it came
    // from), keeping the file's tilemap, tile overrides, systems and render
    // settings.
    [[nodiscard]] bool Save(std::string_view path = {});

    [[nodiscard]] const std::string& GetPath() const { return _path; }

private:
    std::string          _path;
    nlohmann::json       _document;
    SceneFile::IdMap     _ids;
    std::optional<Color> _ambient;
    bool                 _loaded = false;
};
} // namespace Arcbit
