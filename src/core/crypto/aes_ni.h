// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// aes_ni.h — AES-128/256 primitives with runtime CPU dispatch
//
// Dispatch hierarchy for CTR (the hot path):
//
//   CPU has AVX-512F + VAES  →  Ctr128_vaes512   (~19 GB/s on Zen 4)
//   CPU has AVX2    + VAES   →  Ctr128_vaes256   (~15 GB/s on Zen 3-4)
//   CPU has AES-NI  only     →  Ctr128_sse4      (~ 9 GB/s, any post-2011 x86-64)
//   No hardware AES          →  Ctr128_openssl   (OpenSSL EVP fallback)
//
// All paths produce byte-identical output for the same key/IV.
// Counter format: 16-byte big-endian (byte[0]=MSB), matching Nintendo's
// sector counter convention. The hardware paths use bswap+add_epi64+bswap
// rather than a scalar byte loop, eliminating the original ctr_inc bottleneck.
//
// OpenSSL is the baseline and fallback: Ctr128 falls back to OpenSSL EVP
// when no hardware AES is present at runtime. OpenSSL is also used directly
// for XTS (benchmark) and CMAC cross-checks. In practice the fallback never
// fires on any x86-64 built after 2011, but it makes the code correct on
// hypothetical no-AES-NI VMs or future ports.
//
// XTS and CMAC: single AES-NI tier. XTS dispatches to OpenSSL EVP above
// kXtsOsslThreshold bytes per sector, where OpenSSL's 6-block-interleaved
// asm outperforms the single-block intrinsic loop. Below the threshold the
// intrinsic wins due to zero EVP context-allocation overhead per call.
//
// Compile constraints:
//   - No global -maes / -mavx2 / -mavx512f flags required.
//   - Each function carries its own __attribute__((target(...))).
//   - Runtime CPU detection uses CPUID directly (<cpuid.h>) for portability
//     across Clang and GCC. __builtin_cpu_supports is NOT used because
//     Clang 18 does not recognise "vaes" as a valid feature string.
//   - The CpuFeatures::detect() result is cached in a static local (thread-safe).
//   - Works under Clang and GCC, including llvm-mingw for Windows PE targets
//     (ISA target attributes are not platform-specific).

#pragma once

// aes_ni.h is x86-64 only. On other architectures the entire file is a no-op;
// callers guard usage with #ifdef ARCHITECTURE_x86_64.
#if defined(__x86_64__) || defined(_M_X64)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <wmmintrin.h>   // AES-NI
#include <emmintrin.h>   // SSE2
#include <tmmintrin.h>   // SSSE3: _mm_shuffle_epi8

// VAES / AVX headers — only included if the compiler supports them.
// The target attributes on the individual functions gate actual emission.
#if defined(__GNUC__) || defined(__clang__)
#  include <immintrin.h>  // AVX2, AVX-512F, VAES intrinsics
#endif

// OpenSSL fallback — pulled in only for the non-AES-NI scalar path.
#include <openssl/evp.h>

namespace Core::Crypto::AesNi {

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr std::size_t kBlockSize    = 16;
static constexpr std::size_t kKeySize128   = 16;
static constexpr std::size_t kKeySize256   = 32;
static constexpr std::size_t kRoundKeys128 = 11;
static constexpr std::size_t kRoundKeys256 = 15;

// XTS sector size above which OpenSSL's interleaved asm outperforms the
// single-block intrinsic loop. Measured crossover is between 4KB and 8KB;
// 4096 is the conservative threshold that keeps intrinsics only where they
// are unambiguously faster. See aes_util.cpp Transcode XTS branch.
static constexpr std::size_t kXtsOsslThreshold = 4096;

// ── Internal: bswap128 (SSSE3) ────────────────────────────────────────────────
//
// Reverses byte order of a __m128i.
// Used to convert Nintendo big-endian counter ↔ little-endian arithmetic.
// SSSE3 has been present on every CPU that also has AES-NI (Sandy Bridge+).

__attribute__((target("ssse3")))
static inline __m128i Bswap128(__m128i x) {
    const __m128i kShuffle =
        _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
    return _mm_shuffle_epi8(x, kShuffle);
}

// ── Internal: single-block AES-128 encrypt (SSE/AES-NI) ──────────────────────

__attribute__((target("aes,sse2")))
static inline __m128i EncBlock128(const __m128i* ks, __m128i b) {
    b = _mm_xor_si128(b, ks[0]);
    b = _mm_aesenc_si128(b, ks[1]);  b = _mm_aesenc_si128(b, ks[2]);
    b = _mm_aesenc_si128(b, ks[3]);  b = _mm_aesenc_si128(b, ks[4]);
    b = _mm_aesenc_si128(b, ks[5]);  b = _mm_aesenc_si128(b, ks[6]);
    b = _mm_aesenc_si128(b, ks[7]);  b = _mm_aesenc_si128(b, ks[8]);
    b = _mm_aesenc_si128(b, ks[9]);
    return _mm_aesenclast_si128(b, ks[10]);
}

// ── Key expansion ─────────────────────────────────────────────────────────────

__attribute__((target("aes,sse2")))
inline void KeyExpand128Enc(const uint8_t* key, __m128i* out_ks) {
    out_ks[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
#define KE128(i, rcon)                                                   \
    {                                                                    \
        __m128i _t = _mm_aeskeygenassist_si128(out_ks[(i)-1], (rcon)); \
        _t = _mm_shuffle_epi32(_t, 0xFF);                               \
        __m128i _s = out_ks[(i)-1];                                     \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        out_ks[i] = _mm_xor_si128(_s, _t);                             \
    }
    KE128(1,0x01) KE128(2,0x02) KE128(3,0x04) KE128(4,0x08) KE128(5,0x10)
    KE128(6,0x20) KE128(7,0x40) KE128(8,0x80) KE128(9,0x1B) KE128(10,0x36)
#undef KE128
}

__attribute__((target("aes,sse2")))
inline void KeyExpand128Dec(const __m128i* enc_ks, __m128i* out_ks) {
    out_ks[0]  = enc_ks[10];
    for (int i = 1; i < 10; ++i)
        out_ks[i] = _mm_aesimc_si128(enc_ks[10 - i]);
    out_ks[10] = enc_ks[0];
}

__attribute__((target("aes,sse2")))
inline void KeyExpand256Enc(const uint8_t* key, __m128i* out_ks) {
    out_ks[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
    out_ks[1] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key + 16));
#define KE256A(i, rcon)                                                  \
    {                                                                    \
        __m128i _t = _mm_aeskeygenassist_si128(out_ks[(i)-1], (rcon)); \
        _t = _mm_shuffle_epi32(_t, 0xFF);                               \
        __m128i _s = out_ks[(i)-2];                                     \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        out_ks[i] = _mm_xor_si128(_s, _t);                             \
    }
#define KE256B(i)                                                        \
    {                                                                    \
        __m128i _t = _mm_aeskeygenassist_si128(out_ks[(i)-1], 0x00);   \
        _t = _mm_shuffle_epi32(_t, 0xAA);                               \
        __m128i _s = out_ks[(i)-2];                                     \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        _s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));                 \
        out_ks[i] = _mm_xor_si128(_s, _t);                             \
    }
    KE256A(2,0x01)  KE256B(3)   KE256A(4,0x02)  KE256B(5)
    KE256A(6,0x04)  KE256B(7)   KE256A(8,0x08)  KE256B(9)
    KE256A(10,0x10) KE256B(11)  KE256A(12,0x20) KE256B(13)
    KE256A(14,0x40)
#undef KE256A
#undef KE256B
}

__attribute__((target("aes,sse2")))
inline void KeyExpand256Dec(const __m128i* enc_ks, __m128i* out_ks) {
    out_ks[0] = enc_ks[14];
    for (int i = 1; i < 14; ++i)
        out_ks[i] = _mm_aesimc_si128(enc_ks[14 - i]);
    out_ks[14] = enc_ks[0];
}

// ── ECB ──────────────────────────────────────────────────────────────────────

__attribute__((target("aes,sse2")))
inline void EcbEncBlock(const __m128i* ks, int rounds,
                        const uint8_t* in, uint8_t* out) {
    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in));
    b = _mm_xor_si128(b, ks[0]);
    for (int r = 1; r < rounds - 1; ++r)
        b = _mm_aesenc_si128(b, ks[r]);
    b = _mm_aesenclast_si128(b, ks[rounds - 1]);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), b);
}

__attribute__((target("aes,sse2")))
inline void EcbDecBlock(const __m128i* ks, int rounds,
                        const uint8_t* in, uint8_t* out) {
    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in));
    b = _mm_xor_si128(b, ks[0]);
    for (int r = 1; r < rounds - 1; ++r)
        b = _mm_aesdec_si128(b, ks[r]);
    b = _mm_aesdeclast_si128(b, ks[rounds - 1]);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), b);
}

// ── GF(2^128) multiply by x ───────────────────────────────────────────────────
//
// Two conventions — see detailed comments in original version:
//   XTS  (IEEE 1619): LE polynomial, carry byte[i]→byte[i+1], reduce byte[0]
//   CMAC (SP800-38B): BE polynomial, carry byte[i]→byte[i-1], reduce byte[15]

__attribute__((target("sse2")))
inline __m128i Gf128MulXle(__m128i a) {
    const __m128i carry = _mm_srli_epi64(a, 63);
    __m128i shifted = _mm_or_si128(
        _mm_slli_epi64(a, 1),
        _mm_slli_si128(carry, 8));
    const __m128i kPoly = _mm_set_epi32(0, 0, 0, 0x87);
    __m128i hi_carry = _mm_srli_si128(carry, 8);
    __m128i red = _mm_and_si128(kPoly,
        _mm_sub_epi64(_mm_setzero_si128(), hi_carry));
    return _mm_xor_si128(shifted, red);
}

__attribute__((target("sse2")))
inline __m128i Gf128MulXbe(__m128i a) {
    // Scalar: called only during cold-path CMAC subkey generation.
    uint8_t in[kBlockSize], out[kBlockSize];
    _mm_storeu_si128(reinterpret_cast<__m128i*>(in), a);
    const int msb = (in[0] >> 7) & 1;
    for (int i = 0; i < 15; ++i)
        out[i] = static_cast<uint8_t>((in[i] << 1) | (in[i + 1] >> 7));
    out[15] = static_cast<uint8_t>(in[15] << 1);
    if (msb) out[15] ^= 0x87;
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(out));
}

// ── CTR implementation: OpenSSL EVP fallback ─────────────────────────────────
//
// Used when neither AES-NI nor VAES is present at runtime. OpenSSL is the
// baseline: correct on any platform, and fast when hardware AES is present
// (OpenSSL's own aesni-x86_64.s path). This path fires only on hardware
// without AES-NI — in practice never on any modern x86-64 host, but kept
// for correctness on hypothetical no-hardware-AES VMs.
// The key parameter is the raw 16-byte AES key (not the round key schedule).

namespace detail {

inline void Ctr128_openssl(const uint8_t* raw_key_16, const uint8_t* in,
                            uint8_t* out, std::size_t len,
                            uint8_t ctr[kBlockSize]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    // EVP uses the counter as passed; it handles BE increment internally.
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, raw_key_16, ctr);
    int outl = 0;
    EVP_EncryptUpdate(ctx, out, &outl, in, static_cast<int>(len));
    EVP_CIPHER_CTX_free(ctx);
    // Advance ctr to reflect consumed blocks (mimic in-place counter update).
    std::size_t blocks = (len + kBlockSize - 1) / kBlockSize;
    for (std::size_t b = 0; b < blocks; ++b) {
        for (int i = 15; i >= 0; --i)
            if (++ctr[i] != 0) break;
    }
}

// ── CTR implementation: AES-NI SSE4, fixed counter ───────────────────────────
//
// bswap128 converts the Nintendo BE counter to LE for arithmetic, then back.
// Cost: 2 pshufb per 4-block group = ~2 cycles vs ~64 load/store/branch for
// a scalar byte loop. Four blocks are encrypted in parallel per iteration to
// hide the 4-cycle AES-NI instruction latency (~9 GB/s on AES-NI-only CPUs).

__attribute__((target("aes,ssse3")))
inline void Ctr128_sse4(const __m128i* ks, const uint8_t* in, uint8_t* out,
                         std::size_t len, uint8_t ctr[kBlockSize]) {
    const __m128i ONE  = _mm_set_epi64x(0, 1);
    const __m128i FOUR = _mm_set_epi64x(0, 4);

    // Load BE counter, bswap to LE for arithmetic.
    __m128i cle = Bswap128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ctr)));

    std::size_t i = 0;
    for (; i + 64 <= len; i += 64) {
        // Compute 4 counter values in LE space, bswap each back to BE for AES.
        __m128i c0 = Bswap128(cle);
        __m128i c1 = Bswap128(_mm_add_epi64(cle, ONE));
        __m128i c2 = Bswap128(_mm_add_epi64(cle, _mm_set_epi64x(0, 2)));
        __m128i c3 = Bswap128(_mm_add_epi64(cle, _mm_set_epi64x(0, 3)));
        cle = _mm_add_epi64(cle, FOUR);

        // AddRoundKey + 9 rounds + final, 4-wide interleaved
        c0 = _mm_xor_si128(c0, ks[0]); c1 = _mm_xor_si128(c1, ks[0]);
        c2 = _mm_xor_si128(c2, ks[0]); c3 = _mm_xor_si128(c3, ks[0]);
        for (int r = 1; r <= 9; ++r) {
            c0 = _mm_aesenc_si128(c0, ks[r]); c1 = _mm_aesenc_si128(c1, ks[r]);
            c2 = _mm_aesenc_si128(c2, ks[r]); c3 = _mm_aesenc_si128(c3, ks[r]);
        }
        c0 = _mm_aesenclast_si128(c0, ks[10]); c1 = _mm_aesenclast_si128(c1, ks[10]);
        c2 = _mm_aesenclast_si128(c2, ks[10]); c3 = _mm_aesenclast_si128(c3, ks[10]);

        const auto* src = reinterpret_cast<const __m128i*>(in + i);
        auto*       dst = reinterpret_cast<__m128i*>(out + i);
        _mm_storeu_si128(dst,     _mm_xor_si128(c0, _mm_loadu_si128(src)));
        _mm_storeu_si128(dst + 1, _mm_xor_si128(c1, _mm_loadu_si128(src + 1)));
        _mm_storeu_si128(dst + 2, _mm_xor_si128(c2, _mm_loadu_si128(src + 2)));
        _mm_storeu_si128(dst + 3, _mm_xor_si128(c3, _mm_loadu_si128(src + 3)));
    }
    // Scalar tail
    for (; i + 16 <= len; i += 16) {
        __m128i k = EncBlock128(ks, Bswap128(cle));
        cle = _mm_add_epi64(cle, ONE);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i),
            _mm_xor_si128(k, _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i))));
    }
    // Partial final block
    if (i < len) {
        uint8_t ks_bytes[kBlockSize];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ks_bytes),
            EncBlock128(ks, Bswap128(cle)));
        cle = _mm_add_epi64(cle, ONE);
        for (std::size_t j = 0; j < len - i; ++j)
            out[i + j] = in[i + j] ^ ks_bytes[j];
    }
    // Write back the advanced counter in BE.
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ctr), Bswap128(cle));
}

// ── CTR implementation: VAES + AVX2 (256-bit, 4 blocks/iter) ─────────────────
//
// _mm256_aesenc_epi128 processes two 128-bit lanes simultaneously in a 256-bit
// register. Two ymm registers per loop = 4 blocks per iteration; each AES
// instruction does twice the work of the SSE4 path (~15 GB/s on Zen 3-4).

__attribute__((target("avx2,vaes,ssse3")))
inline void Ctr128_vaes256(const __m128i* ks128, const uint8_t* in, uint8_t* out,
                            std::size_t len, uint8_t ctr[kBlockSize]) {
    // Broadcast each 128-bit round key into a 256-bit register.
    __m256i rk[11];
    for (int i = 0; i <= 10; ++i)
        rk[i] = _mm256_broadcastsi128_si256(ks128[i]);

    const __m128i ONE  = _mm_set_epi64x(0, 1);
    const __m128i FOUR = _mm_set_epi64x(0, 4);
    __m128i cle = Bswap128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ctr)));
    __m128i c0 = cle;
    __m128i c1 = _mm_add_epi64(cle, ONE);
    __m128i c2 = _mm_add_epi64(cle, _mm_set_epi64x(0, 2));
    __m128i c3 = _mm_add_epi64(cle, _mm_set_epi64x(0, 3));

    std::size_t i = 0;
    for (; i + 64 <= len; i += 64) {
        // bswap back to BE, pack pairs into ymm
        __m256i b01 = _mm256_set_m128i(Bswap128(c1), Bswap128(c0));
        __m256i b23 = _mm256_set_m128i(Bswap128(c3), Bswap128(c2));
        c0 = _mm_add_epi64(c0, FOUR); c1 = _mm_add_epi64(c1, FOUR);
        c2 = _mm_add_epi64(c2, FOUR); c3 = _mm_add_epi64(c3, FOUR);

        b01 = _mm256_xor_si256(b01, rk[0]); b23 = _mm256_xor_si256(b23, rk[0]);
        for (int r = 1; r <= 9; ++r) {
            b01 = _mm256_aesenc_epi128(b01, rk[r]);
            b23 = _mm256_aesenc_epi128(b23, rk[r]);
        }
        b01 = _mm256_aesenclast_epi128(b01, rk[10]);
        b23 = _mm256_aesenclast_epi128(b23, rk[10]);

        const auto* src = reinterpret_cast<const __m256i*>(in + i);
        auto*       dst = reinterpret_cast<__m256i*>(out + i);
        _mm256_storeu_si256(dst,     _mm256_xor_si256(b01, _mm256_loadu_si256(src)));
        _mm256_storeu_si256(dst + 1, _mm256_xor_si256(b23, _mm256_loadu_si256(src + 1)));
    }
    _mm256_zeroupper();

    // Scalar tail via SSE4 path (re-pack cle from c0)
    cle = c0;  // c0 is already advanced past consumed blocks
    for (; i + 16 <= len; i += 16) {
        __m128i k = EncBlock128(ks128, Bswap128(cle));
        cle = _mm_add_epi64(cle, ONE);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i),
            _mm_xor_si128(k, _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i))));
    }
    if (i < len) {
        uint8_t ks_bytes[kBlockSize];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ks_bytes),
            EncBlock128(ks128, Bswap128(cle)));
        cle = _mm_add_epi64(cle, ONE);
        for (std::size_t j = 0; j < len - i; ++j)
            out[i + j] = in[i + j] ^ ks_bytes[j];
    }
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ctr), Bswap128(cle));
}

// ── CTR implementation: VAES + AVX-512F (512-bit, 8 blocks/iter) ─────────────
//
// _mm512_aesenc_epi128 processes four 128-bit lanes in a 512-bit register.
// Two zmm registers per loop = 8 blocks per iteration.
// On Zen 4 (Ryzen 7940HS): ~19 GB/s sustained at 128KB+ buffers.

__attribute__((target("avx512f,vaes,ssse3")))
inline void Ctr128_vaes512(const __m128i* ks128, const uint8_t* in, uint8_t* out,
                            std::size_t len, uint8_t ctr[kBlockSize]) {
    // Broadcast each round key into a 512-bit register.
    __m512i rk[11];
    for (int i = 0; i <= 10; ++i)
        rk[i] = _mm512_broadcast_i32x4(ks128[i]);

    const __m128i ONE   = _mm_set_epi64x(0, 1);
    const __m128i EIGHT = _mm_set_epi64x(0, 8);
    __m128i cle = Bswap128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ctr)));

    // Pre-compute 8 starting counters in LE space.
    __m128i c[8];
    c[0] = cle;
    for (int j = 1; j < 8; ++j)
        c[j] = _mm_add_epi64(c[j-1], ONE);

    // Inline helper: bswap 4 LE __m128i counters and pack into one __m512i.
    // Written as a #define rather than a lambda so the target attribute from
    // the enclosing function applies (Clang does not propagate target to lambdas).
#define PACK4_BE(p) \
    _mm512_inserti32x4( \
        _mm512_inserti32x4( \
            _mm512_inserti32x4( \
                _mm512_castsi128_si512(Bswap128((p)[0])), \
                Bswap128((p)[1]), 1), \
            Bswap128((p)[2]), 2), \
        Bswap128((p)[3]), 3)

    std::size_t i = 0;
    for (; i + 128 <= len; i += 128) {
        __m512i k0 = PACK4_BE(c);
        __m512i k1 = PACK4_BE(c + 4);

        // Advance all 8 counters
        for (int j = 0; j < 8; ++j)
            c[j] = _mm_add_epi64(c[j], EIGHT);

        k0 = _mm512_xor_si512(k0, rk[0]); k1 = _mm512_xor_si512(k1, rk[0]);
        for (int r = 1; r <= 9; ++r) {
            k0 = _mm512_aesenc_epi128(k0, rk[r]);
            k1 = _mm512_aesenc_epi128(k1, rk[r]);
        }
        k0 = _mm512_aesenclast_epi128(k0, rk[10]);
        k1 = _mm512_aesenclast_epi128(k1, rk[10]);

        const auto* src = reinterpret_cast<const __m512i*>(in + i);
        auto*       dst = reinterpret_cast<__m512i*>(out + i);
        _mm512_storeu_si512(dst,     _mm512_xor_si512(k0, _mm512_loadu_si512(src)));
        _mm512_storeu_si512(dst + 1, _mm512_xor_si512(k1, _mm512_loadu_si512(src + 1)));
    }
#undef PACK4_BE
    _mm256_zeroupper();

    // Tail via SSE4 — c[0] holds the next LE counter
    cle = c[0];
    for (; i + 16 <= len; i += 16) {
        __m128i k = EncBlock128(ks128, Bswap128(cle));
        cle = _mm_add_epi64(cle, ONE);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i),
            _mm_xor_si128(k, _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i))));
    }
    if (i < len) {
        uint8_t ks_bytes[kBlockSize];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ks_bytes),
            EncBlock128(ks128, Bswap128(cle)));
        cle = _mm_add_epi64(cle, ONE);
        for (std::size_t j = 0; j < len - i; ++j)
            out[i + j] = in[i + j] ^ ks_bytes[j];
    }
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ctr), Bswap128(cle));
}

} // namespace detail

// ── CPU feature detection ─────────────────────────────────────────────────────
//
// Uses CPUID directly rather than __builtin_cpu_supports() because Clang 18
// does not recognise "vaes" as a valid feature string for that builtin.
// The result is computed once and cached in a static local (thread-safe in
// C++11 and later). Overhead after first call: one branch, fully predicted.
//
// Detected flags:
//   aes_ni  — CPUID leaf 1, ECX bit 25  (AES-NI)
//   ssse3   — CPUID leaf 1, ECX bit  9  (SSSE3, required by Bswap128)
//   avx2    — CPUID leaf 7, EBX bit  5  (AVX2)
//   avx512f — CPUID leaf 7, EBX bit 16  (AVX-512 Foundation)
//   vaes    — CPUID leaf 7, ECX bit  9  (VAES / AVX-VAES)
//
// Note: some virtualisation platforms expose VAES instructions in the ISA
// but mask bit 9 of CPUID leaf 7 ECX. The dispatcher therefore also checks
// for avx2/avx512f before attempting a VAES path.

#include <cpuid.h>

namespace detail {

struct CpuFeatures {
    bool aes_ni  = false;
    bool ssse3   = false;
    bool avx2    = false;
    bool avx512f = false;
    bool vaes    = false;

    static const CpuFeatures& get() {
        static const CpuFeatures f = detect();
        return f;
    }

private:
    static CpuFeatures detect() {
        CpuFeatures f;
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            f.aes_ni = (ecx >> 25) & 1;
            f.ssse3  = (ecx >>  9) & 1;
        }
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            f.avx2    = (ebx >>  5) & 1;
            f.avx512f = (ebx >> 16) & 1;
            f.vaes    = (ecx >>  9) & 1;
        }
        return f;
    }
};

} // namespace detail

// ── CTR public interface: runtime dispatch ────────────────────────────────────
//
// Dispatch order (widest/fastest first):
//   VAES-512  (AVX-512F + VAES)  ~19 GB/s on Zen 4 / Sapphire Rapids
//   VAES-256  (AVX2    + VAES)   ~15 GB/s on Zen 3-4 / Tiger Lake+
//   SSE4      (AES-NI + SSSE3)   ~ 9 GB/s on any post-2011 x86-64
//   OpenSSL   (EVP fallback)     used when no hardware AES is present
//
// All paths produce byte-identical output (verified by NIST vectors and
// cross-agreement tests). ctr[] is modified in-place to reflect consumed blocks.
//
// raw_key_16: only used by the OpenSSL fallback path.
// aes_util.cpp stores it in CipherContext for exactly this purpose.

inline void Ctr128(const __m128i* ks, const uint8_t* in, uint8_t* out,
                   std::size_t len, uint8_t ctr[kBlockSize],
                   const uint8_t* raw_key_16 = nullptr) {
    const auto& cpu = detail::CpuFeatures::get();

    if (cpu.vaes && cpu.avx512f) {
        detail::Ctr128_vaes512(ks, in, out, len, ctr);
    } else if (cpu.vaes && cpu.avx2) {
        detail::Ctr128_vaes256(ks, in, out, len, ctr);
    } else if (cpu.aes_ni) {
        detail::Ctr128_sse4(ks, in, out, len, ctr);
    } else {
        // OpenSSL EVP fallback — no hardware AES.
        if (raw_key_16)
            detail::Ctr128_openssl(raw_key_16, in, out, len, ctr);
    }
}

// ── XTS ──────────────────────────────────────────────────────────────────────
//
// XTS is called once per sector with a fresh tweak per call.
// The single-block intrinsic loop (Xts128Enc/Dec) wins at small sectors
// due to zero EVP context overhead. OpenSSL's 6-block-interleaved asm wins
// at large sectors. aes_util.cpp dispatches based on kXtsOsslThreshold:
//   size <= kXtsOsslThreshold  ->  Xts128Enc / Xts128Dec  (intrinsics)
//   size >  kXtsOsslThreshold  ->  EVP_aes_128_xts         (OpenSSL)

__attribute__((target("aes,sse2")))
__attribute__((target("aes,ssse3")))
inline void Xts128Enc(const __m128i* data_ks, const __m128i* tweak_ks,
                      const uint8_t* tweak_val,
                      const uint8_t* in, uint8_t* out, std::size_t len) {
    __m128i T = EncBlock128(tweak_ks,
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(tweak_val)));
    for (std::size_t i = 0; i < len; i += kBlockSize) {
        __m128i P = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i));
        __m128i X = EncBlock128(data_ks, _mm_xor_si128(P, T));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), _mm_xor_si128(X, T));
        T = Gf128MulXle(T);
    }
}

// DecBlock128: AES-128 decrypt using a 11-entry key schedule produced by
// KeyExpand128Dec. XTS data keys are always AES-128 regardless of whether
// the combined key material is 256-bit.
__attribute__((target("aes,sse2")))
static inline __m128i DecBlock128(const __m128i* ks, __m128i b) {
    b = _mm_xor_si128(b, ks[0]);
    b = _mm_aesdec_si128(b, ks[1]);  b = _mm_aesdec_si128(b, ks[2]);
    b = _mm_aesdec_si128(b, ks[3]);  b = _mm_aesdec_si128(b, ks[4]);
    b = _mm_aesdec_si128(b, ks[5]);  b = _mm_aesdec_si128(b, ks[6]);
    b = _mm_aesdec_si128(b, ks[7]);  b = _mm_aesdec_si128(b, ks[8]);
    b = _mm_aesdec_si128(b, ks[9]);
    return _mm_aesdeclast_si128(b, ks[10]);
}

__attribute__((target("aes,ssse3")))
inline void Xts128Dec(const __m128i* data_dec_ks, const __m128i* tweak_ks,
                      const uint8_t* tweak_val,
                      const uint8_t* in, uint8_t* out, std::size_t len) {
    __m128i T = EncBlock128(tweak_ks,
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(tweak_val)));
    for (std::size_t i = 0; i < len; i += kBlockSize) {
        __m128i C = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i));
        __m128i X = DecBlock128(data_dec_ks, _mm_xor_si128(C, T));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), _mm_xor_si128(X, T));
        T = Gf128MulXle(T);
    }
}

// ── CMAC ──────────────────────────────────────────────────────────────────────
//
// Cold-path only (key loading and verification). AES-NI SSE2 is sufficient.

__attribute__((target("aes,sse2")))
inline void Cmac128(const __m128i* ks, const uint8_t* msg, std::size_t len,
                    uint8_t* out_tag) {
    __m128i L  = EncBlock128(ks, _mm_setzero_si128());
    __m128i K1 = Gf128MulXbe(L);
    __m128i K2 = Gf128MulXbe(K1);

    __m128i X = _mm_setzero_si128();
    const std::size_t full_blocks = (len > 0) ? (len - 1) / kBlockSize : 0;
    for (std::size_t b = 0; b < full_blocks; ++b) {
        __m128i M = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(msg + b * kBlockSize));
        X = EncBlock128(ks, _mm_xor_si128(X, M));
    }

    const std::size_t remainder = len - full_blocks * kBlockSize;
    uint8_t last[kBlockSize] = {};
    if (len > 0 && remainder == kBlockSize) {
        std::memcpy(last, msg + full_blocks * kBlockSize, kBlockSize);
        __m128i M = _mm_loadu_si128(reinterpret_cast<const __m128i*>(last));
        X = _mm_xor_si128(X, _mm_xor_si128(M, K1));
    } else {
        if (remainder > 0)
            std::memcpy(last, msg + full_blocks * kBlockSize, remainder);
        last[remainder] = 0x80;
        __m128i M = _mm_loadu_si128(reinterpret_cast<const __m128i*>(last));
        X = _mm_xor_si128(X, _mm_xor_si128(M, K2));
    }
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out_tag), EncBlock128(ks, X));
}

} // namespace Core::Crypto::AesNi

#endif // defined(__x86_64__) || defined(_M_X64)
