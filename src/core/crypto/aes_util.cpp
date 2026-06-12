// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/params.h>
#include "common/assert.h"
#include "common/logging.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/key_manager.h"

#ifdef ARCHITECTURE_x86_64
#include "core/crypto/aes_ni.h"
#endif

namespace Core::Crypto {
namespace {

using NintendoTweak = std::array<u8, 16>;

NintendoTweak CalculateNintendoTweak(std::size_t sector_id) {
    NintendoTweak out{};
    for (std::size_t i = 0xF; i <= 0xF; --i) {
        out[i] = sector_id & 0xFF;
        sector_id >>= 8;
    }
    return out;
}

} // Anonymous namespace

// ── CipherContext ─────────────────────────────────────────────────────────────
// On x86-64: precomputed AES-NI round key schedules + raw key for fallback.
// On other architectures: raw key bytes only; all operations use OpenSSL EVP.

struct CipherContext {
#ifdef ARCHITECTURE_x86_64
    // Encrypt schedule: 15 slots covers AES-128 (11 used) and AES-256 (15 used)
    __m128i ks_enc[AesNi::kRoundKeys256];
    // Decrypt schedule: same layout
    __m128i ks_dec[AesNi::kRoundKeys256];
    // XTS only: separate 128-bit encrypt schedule for the tweak key
    __m128i ks_tweak[AesNi::kRoundKeys128];
    // Number of active round keys: 11 for AES-128, 15 for AES-256
    int rounds;
#endif
    // Raw key bytes used by OpenSSL fallback paths (CTR, XTS large sectors,
    // and all operations on non-x86 platforms).
    // 32 bytes covers both AES-128 and AES-256 key material.
    uint8_t raw_key[32];
    std::size_t key_size; // bytes: 16 or 32
    // Current IV as a 16-byte big-endian value (CTR counter or XTS tweak).
    uint8_t iv[16];
    Mode mode;
};

template <typename Key, std::size_t KeySize>
AESCipher<Key, KeySize>::AESCipher(Key key, Mode mode)
    : ctx(std::make_unique<CipherContext>()) {

    ctx->mode = mode;
    ctx->key_size = KeySize;
    std::memset(ctx->iv, 0, 16);
    std::memcpy(ctx->raw_key, key.data(), KeySize);

#ifdef ARCHITECTURE_x86_64
    if constexpr (KeySize == AesNi::kKeySize128) {
        ctx->rounds = static_cast<int>(AesNi::kRoundKeys128);
        AesNi::KeyExpand128Enc(key.data(), ctx->ks_enc);
        AesNi::KeyExpand128Dec(ctx->ks_enc, ctx->ks_dec);
        // For XTS: raw_key[16..31] mirrors [0..15] (same key for data and tweak).
        std::memcpy(ctx->raw_key + 16, key.data(), AesNi::kKeySize128);

        if (mode == Mode::XTS) {
            // Key128 + XTS: same 16-byte key for data and tweak.
            // Non-standard but matches existing yuzu/citron behaviour;
            // in practice Key128+XTS is never instantiated (all XTS callers
            // use Key256).
            AesNi::KeyExpand128Enc(key.data(), ctx->ks_tweak);
        }
    } else {
        // AES-256 key material — but XTS-AES-128 uses two independent 128-bit
        // keys: key[0..15] for data, key[16..31] for tweak. Expand both as
        // AES-128 (11 round keys each). The full 256-bit AES-256 key schedule
        // is never used in XTS mode.
        ctx->rounds = static_cast<int>(AesNi::kRoundKeys256);
        if (mode == Mode::XTS) {
            // XTS: data key = key[0..15], tweak key = key[16..31], both AES-128.
            AesNi::KeyExpand128Enc(key.data(), ctx->ks_enc);
            AesNi::KeyExpand128Dec(ctx->ks_enc, ctx->ks_dec);
            AesNi::KeyExpand128Enc(key.data() + AesNi::kKeySize128, ctx->ks_tweak);
        } else {
            // ECB/CTR: use full AES-256 key schedule.
            AesNi::KeyExpand256Enc(key.data(), ctx->ks_enc);
            AesNi::KeyExpand256Dec(ctx->ks_enc, ctx->ks_dec);
        }
    }
#endif // ARCHITECTURE_x86_64
}

template <typename Key, std::size_t KeySize>
AESCipher<Key, KeySize>::~AESCipher() = default;

template <typename Key, std::size_t KeySize>
void AESCipher<Key, KeySize>::SetIV(std::span<const u8> data) {
    ASSERT_MSG(data.size() == 16, "IV must be exactly 16 bytes");
    std::memcpy(ctx->iv, data.data(), 16);
}

template <typename Key, std::size_t KeySize>
void AESCipher<Key, KeySize>::Transcode(const u8* src, std::size_t size, u8* dest,
                                        Op op) const {
    switch (ctx->mode) {
    case Mode::ECB: {
#ifdef ARCHITECTURE_x86_64
        if (size < AesNi::kBlockSize) {
            u8 block[AesNi::kBlockSize] = {};
            std::memcpy(block, src, size);
            if (op == Op::Encrypt)
                AesNi::EcbEncBlock(ctx->ks_enc, ctx->rounds, block, block);
            else
                AesNi::EcbDecBlock(ctx->ks_dec, ctx->rounds, block, block);
            std::memcpy(dest, block, size);
            return;
        }
        for (std::size_t off = 0; off < size; off += AesNi::kBlockSize) {
            const std::size_t chunk = std::min(AesNi::kBlockSize, size - off);
            if (chunk < AesNi::kBlockSize) {
                u8 block[AesNi::kBlockSize] = {};
                std::memcpy(block, src + off, chunk);
                if (op == Op::Encrypt)
                    AesNi::EcbEncBlock(ctx->ks_enc, ctx->rounds, block, block);
                else
                    AesNi::EcbDecBlock(ctx->ks_dec, ctx->rounds, block, block);
                std::memcpy(dest + off, block, chunk);
            } else {
                if (op == Op::Encrypt)
                    AesNi::EcbEncBlock(ctx->ks_enc, ctx->rounds, src + off, dest + off);
                else
                    AesNi::EcbDecBlock(ctx->ks_dec, ctx->rounds, src + off, dest + off);
            }
        }
#else
        // Non-x86: OpenSSL EVP ECB
        {
            const EVP_CIPHER* cipher = (ctx->key_size == 16)
                ? (op == Op::Encrypt ? EVP_aes_128_ecb() : EVP_aes_128_ecb())
                : (op == Op::Encrypt ? EVP_aes_256_ecb() : EVP_aes_256_ecb());
            EVP_CIPHER_CTX* evp = EVP_CIPHER_CTX_new();
            if (op == Op::Encrypt) {
                EVP_EncryptInit_ex(evp, cipher, nullptr, ctx->raw_key, nullptr);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0;
                for (std::size_t off = 0; off < size; off += 16) {
                    const int chunk = static_cast<int>(std::min<std::size_t>(16, size - off));
                    u8 block[16] = {};
                    std::memcpy(block, src + off, chunk);
                    EVP_EncryptUpdate(evp, dest + off, &outl, block, 16);
                }
            } else {
                EVP_DecryptInit_ex(evp, cipher, nullptr, ctx->raw_key, nullptr);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0;
                for (std::size_t off = 0; off < size; off += 16) {
                    const int chunk = static_cast<int>(std::min<std::size_t>(16, size - off));
                    u8 block[16] = {};
                    std::memcpy(block, src + off, chunk);
                    EVP_DecryptUpdate(evp, dest + off, &outl, block, 16);
                }
            }
            EVP_CIPHER_CTX_free(evp);
        }
#endif
        break;
    }
    case Mode::CTR: {
#ifdef ARCHITECTURE_x86_64
        ASSERT_MSG(ctx->rounds == static_cast<int>(AesNi::kRoundKeys128),
                   "CTR mode requires AES-128 key schedule");
        uint8_t ctr_copy[AesNi::kBlockSize];
        std::memcpy(ctr_copy, ctx->iv, AesNi::kBlockSize);
        AesNi::Ctr128(ctx->ks_enc, src, dest, size, ctr_copy, ctx->raw_key);
#else
        {
            EVP_CIPHER_CTX* evp = EVP_CIPHER_CTX_new();
            EVP_EncryptInit_ex(evp, EVP_aes_128_ctr(), nullptr, ctx->raw_key, ctx->iv);
            int outl = 0;
            EVP_EncryptUpdate(evp, dest, &outl, src, static_cast<int>(size));
            EVP_CIPHER_CTX_free(evp);
        }
#endif
        break;
    }
    case Mode::XTS: {
#ifdef ARCHITECTURE_x86_64
        // Below kXtsOsslThreshold: intrinsic loop wins (zero EVP overhead).
        // Above it: OpenSSL's 6-block-interleaved asm is faster.
        if (size <= AesNi::kXtsOsslThreshold) {
            if (op == Op::Encrypt)
                AesNi::Xts128Enc(ctx->ks_enc, ctx->ks_tweak, ctx->iv, src, dest, size);
            else
                AesNi::Xts128Dec(ctx->ks_dec, ctx->ks_tweak, ctx->iv, src, dest, size);
        } else {
            EVP_CIPHER_CTX* evp = EVP_CIPHER_CTX_new();
            if (op == Op::Encrypt) {
                EVP_EncryptInit_ex(evp, EVP_aes_128_xts(), nullptr, ctx->raw_key, ctx->iv);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0, outl2 = 0;
                EVP_EncryptUpdate(evp, dest, &outl, src, static_cast<int>(size));
                EVP_EncryptFinal_ex(evp, dest + outl, &outl2);
            } else {
                EVP_DecryptInit_ex(evp, EVP_aes_128_xts(), nullptr, ctx->raw_key, ctx->iv);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0, outl2 = 0;
                EVP_DecryptUpdate(evp, dest, &outl, src, static_cast<int>(size));
                EVP_DecryptFinal_ex(evp, dest + outl, &outl2);
            }
            EVP_CIPHER_CTX_free(evp);
        }
#else
        // Non-x86: always use OpenSSL EVP XTS
        {
            EVP_CIPHER_CTX* evp = EVP_CIPHER_CTX_new();
            if (op == Op::Encrypt) {
                EVP_EncryptInit_ex(evp, EVP_aes_128_xts(), nullptr, ctx->raw_key, ctx->iv);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0, outl2 = 0;
                EVP_EncryptUpdate(evp, dest, &outl, src, static_cast<int>(size));
                EVP_EncryptFinal_ex(evp, dest + outl, &outl2);
            } else {
                EVP_DecryptInit_ex(evp, EVP_aes_128_xts(), nullptr, ctx->raw_key, ctx->iv);
                EVP_CIPHER_CTX_set_padding(evp, 0);
                int outl = 0, outl2 = 0;
                EVP_DecryptUpdate(evp, dest, &outl, src, static_cast<int>(size));
                EVP_DecryptFinal_ex(evp, dest + outl, &outl2);
            }
            EVP_CIPHER_CTX_free(evp);
        }
#endif
        break;
    }
    default:
        ASSERT_MSG(false, "Unknown AES mode");
    }
}

template <typename Key, std::size_t KeySize>
void AESCipher<Key, KeySize>::XTSTranscode(const u8* src, std::size_t size, u8* dest,
                                           std::size_t sector_id, std::size_t sector_size,
                                           Op op) {
    ASSERT_MSG(size % sector_size == 0, "XTS size must be a multiple of sector_size");
    for (std::size_t i = 0; i < size; i += sector_size) {
        SetIV(CalculateNintendoTweak(sector_id++));
        Transcode(src + i, sector_size, dest + i, op);
    }
}

template class AESCipher<Key128>;
template class AESCipher<Key256>;

} // namespace Core::Crypto
