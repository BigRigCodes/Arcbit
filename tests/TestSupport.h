#pragma once

// Shared helpers for arcbit-tests: scratch directories, source-tree asset
// paths, and Catch2 printers so failed comparisons of engine types show values
// instead of "{?}".

#include <arcbit/core/Math.h>

#include <catch2/catch_tostring.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace ArcbitTests
{
// Absolute path to a file in the source tree (ARCBIT_SOURCE_DIR is set by
// tests/CMakeLists.txt). Used to validate shipped assets in place.
inline std::filesystem::path SourcePath(const std::string_view relative)
{
    return std::filesystem::path(ARCBIT_SOURCE_DIR) / relative;
}

// A fresh directory under the system temp dir, removed (with its contents)
// when the object goes out of scope.
class TempDir
{
public:
    TempDir()
    {
        static int counter = 0;
        _path = std::filesystem::temp_directory_path() /
                std::format("arcbit-tests-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), ++counter);
        std::filesystem::create_directories(_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec); // best effort; never throw from a destructor
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] std::filesystem::path File(const std::string_view name) const { return _path / name; }

    // Write `contents` to `name` inside the directory and return its full path as a string.
    std::string Write(const std::string_view name, const std::string_view contents) const
    {
        const std::filesystem::path p = File(name);
        std::ofstream(p, std::ios::binary) << contents;
        return p.string();
    }

private:
    std::filesystem::path _path;
};
} // namespace ArcbitTests

template<>
struct Catch::StringMaker<Arcbit::Vec2>
{
    static std::string convert(const Arcbit::Vec2& v) { return std::format("({}, {})", v.X, v.Y); }
};

template<>
struct Catch::StringMaker<Arcbit::Color>
{
    static std::string convert(const Arcbit::Color& c) { return std::format("rgba({}, {}, {}, {})", c.R, c.G, c.B, c.A); }
};
