// SPDX-FileCopyrightText: Copyright 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// JWT RS256 verification using OpenSSL EVP directly.
// Replaces cpp-jwt v1.4 (which was hardwired to OpenSSL anyway).
// Only RS256 (RSA-SHA256 PKCS#1 v1.5) is needed; sign path is unused.

#include <array>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "common/logging.h"
#include "network/verify_user.h"
#include "web_service/verify_user_jwt.h"
#include "web_service/web_backend.h"
#include "web_service/web_result.h"

namespace WebService {

namespace {

// ── Base64url helpers ─────────────────────────────────────────────────────────

// Standard base64 alphabet + url-safe variant (-_ instead of +/)
// JWT uses base64url without padding.

static const std::string kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Decode base64 or base64url (interchangeable here via char mapping).
// Handles missing padding. Returns decoded bytes or empty on error.
static std::vector<uint8_t> Base64UrlDecode(std::string_view input) {
    std::string s(input);
    // Map url-safe chars to standard base64
    for (char& c : s) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
    }
    // Add padding
    while (s.size() % 4 != 0) s += '=';

    // Use OpenSSL EVP_DecodeBlock
    // EVP_DecodeBlock strips whitespace and handles padding.
    std::vector<uint8_t> out(s.size());  // upper bound
    int len = EVP_DecodeBlock(out.data(),
                              reinterpret_cast<const uint8_t*>(s.data()),
                              static_cast<int>(s.size()));
    if (len < 0) return {};
    // Remove padding bytes from count
    if (!s.empty() && s[s.size()-1] == '=') len--;
    if (s.size() >= 2 && s[s.size()-2] == '=') len--;
    out.resize(static_cast<size_t>(len));
    return out;
}

// ── JWT parsing ───────────────────────────────────────────────────────────────

struct JwtParts {
    std::string header_b64;
    std::string payload_b64;
    std::vector<uint8_t> signature;
    // header.payload as bytes (the signed content)
    std::string signed_input;
};

static std::optional<JwtParts> SplitJwt(const std::string& token) {
    const auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    const auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    JwtParts p;
    p.header_b64  = token.substr(0, dot1);
    p.payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    p.signed_input = token.substr(0, dot2);  // "header.payload"

    const std::string sig_b64 = token.substr(dot2 + 1);
    p.signature = Base64UrlDecode(sig_b64);
    if (p.signature.empty()) return std::nullopt;

    return p;
}

// ── RS256 verification ────────────────────────────────────────────────────────

// Verifies RS256 (RSASSA-PKCS1-v1_5 with SHA-256).
// pub_key_pem: PEM-encoded RSA public key (PKCS#8 SubjectPublicKeyInfo or PKCS#1).
// signed_input: the "header.payload" bytes that were signed.
// signature: raw RSA signature bytes.
static bool VerifyRS256(const std::string& pub_key_pem,
                        const std::string& signed_input,
                        const std::vector<uint8_t>& signature) {
    // Load public key from PEM
    BIO* bio = BIO_new_mem_buf(pub_key_pem.data(), static_cast<int>(pub_key_pem.size()));
    if (!bio) return false;

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        LOG_ERROR(WebService, "Failed to load JWT public key: {}",
                  ERR_error_string(ERR_get_error(), nullptr));
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        EVP_DigestVerifyUpdate(ctx,
            reinterpret_cast<const uint8_t*>(signed_input.data()),
            signed_input.size()) == 1 &&
        EVP_DigestVerifyFinal(ctx, signature.data(), signature.size()) == 1) {
        ok = true;
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

} // namespace

// ── Public interface ──────────────────────────────────────────────────────────

std::string GetPublicKey(const std::string& host) {
    // Fetch the public key from the backend's /jwt/key endpoint.
    // This is unchanged from the original implementation.
    Client client(host, {}, {});
    auto response = client.GetJson("/jwt/key", true);
    if (response.result_code != WebResult::Code::Success) {
        LOG_ERROR(WebService, "Failed to retrieve JWT public key from {}", host);
        return {};
    }
    return response.returned_data;
}

VerifyUserJWT::VerifyUserJWT(const std::string& host) : pub_key(GetPublicKey(host)) {}

Network::VerifyUser::UserData VerifyUserJWT::LoadUserData(const std::string& verify_uid,
                                                          const std::string& token) {
    if (pub_key.empty()) {
        LOG_ERROR(WebService, "Public key unavailable; cannot verify JWT");
        return {};
    }

    // Split the JWT
    const auto parts = SplitJwt(token);
    if (!parts) {
        LOG_ERROR(WebService, "Malformed JWT token");
        return {};
    }

    // Verify signature
    if (!VerifyRS256(pub_key, parts->signed_input, parts->signature)) {
        LOG_ERROR(WebService, "JWT signature verification failed");
        return {};
    }

    // Decode and parse the payload
    const auto payload_bytes = Base64UrlDecode(parts->payload_b64);
    if (payload_bytes.empty()) {
        LOG_ERROR(WebService, "Failed to decode JWT payload");
        return {};
    }

    try {
        const std::string payload_str(payload_bytes.begin(), payload_bytes.end());
        const auto payload = nlohmann::json::parse(payload_str);

        Network::VerifyUser::UserData user_data;
        user_data.username    = payload.value("username", "");
        user_data.display_name = payload.value("display_name", "");
        user_data.avatar_url  = payload.value("avatar_url", "");
        user_data.moderator    = payload.value("administrator", false);
        return user_data;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(WebService, "Failed to parse JWT payload JSON: {}", e.what());
        return {};
    }
}

} // namespace WebService
