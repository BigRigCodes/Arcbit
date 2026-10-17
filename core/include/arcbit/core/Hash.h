#pragma once

#include <arcbit/core/Types.h>

#include <string_view>

namespace Arcbit
{
// FNV-1a hash of a name - the same value at compile time or runtime, so ids
// built from names (input actions, events) are constants in code and cost no
// string work on hot paths. Collisions are astronomically unlikely for short
// names, but prefer distinctive ones ("Inventory_Open", not "Open").
constexpr u32 HashName(const std::string_view name)
{
    u32 hash = 2166136261u; // FNV offset basis
    for (const unsigned char c : name) {
        hash ^= c;
        hash *= 16777619u; // FNV prime
    }
    return hash;
}
} // namespace Arcbit
