#include <arcbit/save/SaveSeal.h>

#include <arcbit/core/Log.h>

#include <sodium.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace Arcbit::SaveSeal
{
namespace
{
    constexpr std::array<u8, 8> Magic        = { 'A', 'R', 'C', 'S', 'A', 'V', 'E', 0 };
    constexpr u8                Format       = 1;
    constexpr size_t            NonceSize    = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24
    constexpr size_t            HeaderSize   = Magic.size() + 1 + NonceSize;
    constexpr int               ZstdLevel    = 3;
    constexpr u64               MaxSaveBytes = 256ull << 20; // refuse to inflate a "save" past 256 MB

    static_assert(crypto_aead_xchacha20poly1305_ietf_KEYBYTES == std::tuple_size_v<Key>);

    void InitSodium()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            if (sodium_init() < 0) LOG_CRITICAL(Engine, "SaveSeal: libsodium failed to initialize");
        });
    }

    std::vector<u8> Compress(const std::string_view text)
    {
        std::vector<u8> out(ZSTD_compressBound(text.size()));
        const size_t    size = ZSTD_compress(out.data(), out.size(), text.data(), text.size(), ZstdLevel);
        out.resize(ZSTD_isError(size) ? 0 : size);
        return out;
    }

    std::optional<std::string> Decompress(const std::span<const u8> data)
    {
        const unsigned long long size = ZSTD_getFrameContentSize(data.data(), data.size());
        if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > MaxSaveBytes) return std::nullopt;
        std::string text(size, '\0');
        const size_t got = ZSTD_decompress(text.data(), text.size(), data.data(), data.size());
        if (ZSTD_isError(got) || got != size) return std::nullopt;
        return text;
    }
} // namespace

Key DeriveKey(const std::string_view secret)
{
    InitSodium();
    constexpr std::string_view Context = "arcbit.save.v1";
    Key                        key{};
    crypto_generichash(key.data(), key.size(), reinterpret_cast<const u8*>(secret.data()), secret.size(),
                       reinterpret_cast<const u8*>(Context.data()), Context.size());
    return key;
}

std::vector<u8> Seal(const std::string_view json, const Key& key)
{
    InitSodium();
    const std::vector<u8> plain = Compress(json);

    std::vector<u8> file(HeaderSize + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    std::ranges::copy(Magic, file.begin());
    file[Magic.size()] = Format;
    u8* nonce          = file.data() + Magic.size() + 1;
    randombytes_buf(nonce, NonceSize);

    unsigned long long cipherSize = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(file.data() + HeaderSize, &cipherSize, plain.data(), plain.size(),
                                               file.data(), HeaderSize, // the header is authenticated too
                                               nullptr, nonce, key.data());
    file.resize(HeaderSize + cipherSize);
    return file;
}

std::optional<std::string> Open(const std::span<const u8> file, const Key& key)
{
    InitSodium();
    if (!IsSealed(file) || file.size() < HeaderSize + crypto_aead_xchacha20poly1305_ietf_ABYTES) return std::nullopt;
    if (file[Magic.size()] != Format) return std::nullopt;

    const std::span<const u8> cipher = file.subspan(HeaderSize);
    std::vector<u8>           plain(cipher.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long        plainSize = 0;
    const u8*                 nonce     = file.data() + Magic.size() + 1;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plainSize, nullptr, cipher.data(), cipher.size(),
                                                   file.data(), HeaderSize, nonce, key.data()) != 0)
        return std::nullopt; // tampered, truncated, or another key
    return Decompress(std::span(plain.data(), plainSize));
}

bool IsSealed(const std::span<const u8> file)
{
    return file.size() >= Magic.size() && std::equal(Magic.begin(), Magic.end(), file.begin());
}
} // namespace Arcbit::SaveSeal
