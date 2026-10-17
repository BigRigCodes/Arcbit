#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Entity - generational index that prevents use-after-free. Its own header
// so component headers can hold entity references without pulling in World.
// ---------------------------------------------------------------------------
struct Entity
{
    u32 Index      = ~0u;
    u32 Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != ~0u; }

    bool operator==(const Entity& o) const { return Index == o.Index && Generation == o.Generation; }
    bool operator!=(const Entity& o) const { return !(*this == o); }

    static Entity Invalid() { return {}; }
};
} // namespace Arcbit
