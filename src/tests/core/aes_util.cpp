// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "core/crypto/aes_util.h"

namespace {
constexpr std::array<u8, 16> key{0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
constexpr std::array<u8, 16> plaintext{0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                                       0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
} // namespace

TEST_CASE("AESCipher: AES-128 ECB known vector", "[core][crypto]") {
    constexpr std::array<u8, 16> expected{0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
                                          0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97};
    Core::Crypto::AESCipher cipher(key, Core::Crypto::Mode::ECB);
    std::array<u8, 16> encrypted{}, decrypted{};
    cipher.Transcode(plaintext.data(), plaintext.size(), encrypted.data(), Core::Crypto::Op::Encrypt);
    REQUIRE(encrypted == expected);
    cipher.Transcode(encrypted.data(), encrypted.size(), decrypted.data(), Core::Crypto::Op::Decrypt);
    REQUIRE(decrypted == plaintext);
}

TEST_CASE("AESCipher: AES-128 CTR known vector", "[core][crypto]") {
    constexpr std::array<u8, 16> iv{0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                                    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    constexpr std::array<u8, 16> expected{0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
                                          0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce};
    Core::Crypto::AESCipher cipher(key, Core::Crypto::Mode::CTR);
    cipher.SetIV(iv);
    std::array<u8, 16> encrypted{}, decrypted{};
    cipher.Transcode(plaintext.data(), plaintext.size(), encrypted.data(), Core::Crypto::Op::Encrypt);
    REQUIRE(encrypted == expected);
    cipher.Transcode(encrypted.data(), encrypted.size(), decrypted.data(), Core::Crypto::Op::Decrypt);
    REQUIRE(decrypted == plaintext);
}

TEST_CASE("AESCipher: XTS sector round trip", "[core][crypto]") {
    std::array<u8, 32> xts_key{}, input{}, encrypted{}, decrypted{};
    for (std::size_t i = 0; i < xts_key.size(); ++i) {
        xts_key[i] = static_cast<u8>(i);
        input[i] = static_cast<u8>(i + 1);
    }
    Core::Crypto::AESCipher cipher(xts_key, Core::Crypto::Mode::XTS);
    cipher.XTSTranscode(input.data(), input.size(), encrypted.data(), 7, input.size(),
                       Core::Crypto::Op::Encrypt);
    REQUIRE(encrypted != input);
    cipher.XTSTranscode(encrypted.data(), encrypted.size(), decrypted.data(), 7, input.size(),
                       Core::Crypto::Op::Decrypt);
    REQUIRE(decrypted == input);
}
