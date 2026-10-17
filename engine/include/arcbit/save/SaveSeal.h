#pragma once

#include <arcbit/core/Types.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// SaveSeal - seals a save so it can't be read or edited outside the game.
//
// A sealed file is the save's JSON, compressed (zstd) and encrypted with an
// authenticated cipher (XChaCha20-Poly1305, libsodium):
//
//   "ARCSAVE\0"  8 bytes   magic - what Open looks for
//   format       1 byte    this layout's version (1)
//   nonce        24 bytes  random per write
//   ciphertext   ...       the compressed JSON plus a 16-byte tag
//
// The header is authenticated along with the data, so changing any byte -
// a value, the format, the nonce - makes Open refuse the file, as does the
// wrong key. A text or hex editor sees noise.
//
// This stops casual cheating, not a reverse engineer: the game must open its
// own saves, so the key ships inside it. Phase 36's packing protects the key
// (the dev key; the mod key for moddable games, so the editor's mod mode can
// open saves). Until then a per-game secret set in code stands in.
// ---------------------------------------------------------------------------
namespace SaveSeal
{
    using Key = std::array<u8, 32>;

    // A key from a secret string (BLAKE2b, with an Arcbit-specific context so
    // the same secret used elsewhere doesn't give the same key).
    [[nodiscard]] Key DeriveKey(std::string_view secret);

    // Compress and encrypt `json` into a sealed file's bytes.
    [[nodiscard]] std::vector<u8> Seal(std::string_view json, const Key& key);

    // The JSON back from a sealed file; null if it isn't one, or it fails the
    // check (tampered, truncated, or sealed with another key).
    [[nodiscard]] std::optional<std::string> Open(std::span<const u8> file, const Key& key);

    // Starts with the magic: a sealed file, readable or not.
    [[nodiscard]] bool IsSealed(std::span<const u8> file);
} // namespace SaveSeal
} // namespace Arcbit
