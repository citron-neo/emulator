// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/internal_network/dna_gateway_stub.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/logging.h"

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace Network {
namespace {

constexpr std::array<u8, 4> LoopbackIp{127, 0, 0, 1};
constexpr std::string_view MinimalDnaResponse = R"({"status":"ok"})";

std::mutex g_stub_mutex;
std::atomic<bool> g_stub_started{false};
SSL_CTX* g_server_ctx = nullptr;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;

void CloseNativeSocket(NativeSocket fd) {
    closesocket(fd);
}
#else
using NativeSocket = int;
constexpr NativeSocket InvalidNativeSocket = -1;

void CloseNativeSocket(NativeSocket fd) {
    close(fd);
}
#endif

bool GenerateSelfSignedCertificate(SSL_CTX* ctx) {
    EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", 2048);
    if (!pkey) {
        return false;
    }

    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60L * 24L * 3650L);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("my.2k.com"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_pubkey(cert, pkey);

    if (!X509_sign(cert, pkey, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    const int cert_ok = SSL_CTX_use_certificate(ctx, cert);
    const int key_ok = SSL_CTX_use_PrivateKey(ctx, pkey);
    X509_free(cert);
    EVP_PKEY_free(pkey);
    return cert_ok == 1 && key_ok == 1;
}

bool InitializeServerContext() {
    if (g_server_ctx) {
        return true;
    }

    g_server_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_server_ctx) {
        return false;
    }

    SSL_CTX_set_min_proto_version(g_server_ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(g_server_ctx, SSL_OP_NO_COMPRESSION);
    return GenerateSelfSignedCertificate(g_server_ctx);
}

std::string ComputeWebSocketAccept(const std::string& client_key) {
    static constexpr char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string input = client_key + guid;

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_Digest(input.data(), input.size(), digest, &digest_len, EVP_sha1(), nullptr);

    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, digest, static_cast<int>(digest_len));
    BIO_flush(b64);

    char* data = nullptr;
    const long len = BIO_get_mem_data(mem, &data);
    std::string result(data, static_cast<std::size_t>(len));
    BIO_free_all(b64);
    return result;
}

std::string ExtractWebSocketKey(const std::string& request) {
    static constexpr std::string_view prefix = "Sec-WebSocket-Key: ";
    const auto pos = request.find(prefix);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + prefix.size();
    const auto end = request.find("\r\n", start);
    if (end == std::string::npos) {
        return {};
    }
    return request.substr(start, end - start);
}

void SendWebSocketFrame(SSL* ssl, u8 opcode, std::span<const u8> payload) {
    std::vector<u8> frame;
    frame.push_back(static_cast<u8>(0x80 | (opcode & 0x0F)));
    if (payload.size() <= 125) {
        frame.push_back(static_cast<u8>(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<u8>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<u8>(payload.size() & 0xFF));
    } else {
        frame.push_back(127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<u8>((payload.size() >> shift) & 0xFF));
        }
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    SSL_write(ssl, frame.data(), static_cast<int>(frame.size()));
}

struct ParsedWebSocketFrame {
    u8 opcode{};
    std::vector<u8> payload;
};

std::optional<ParsedWebSocketFrame> TryParseClientFrame(std::vector<u8>& buffer) {
    if (buffer.size() < 2) {
        return std::nullopt;
    }

    const u8 opcode = buffer[0] & 0x0F;
    const bool masked = (buffer[1] & 0x80) != 0;
    u64 payload_len = buffer[1] & 0x7F;
    std::size_t pos = 2;

    if (payload_len == 126) {
        if (buffer.size() < 4) {
            return std::nullopt;
        }
        payload_len = (static_cast<u64>(buffer[2]) << 8) | buffer[3];
        pos = 4;
    } else if (payload_len == 127) {
        if (buffer.size() < 10) {
            return std::nullopt;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | buffer[2 + i];
        }
        pos = 10;
    }

    std::array<u8, 4> mask{};
    if (masked) {
        if (buffer.size() < pos + 4) {
            return std::nullopt;
        }
        std::memcpy(mask.data(), buffer.data() + pos, 4);
        pos += 4;
    }

    if (buffer.size() < pos + payload_len) {
        return std::nullopt;
    }

    ParsedWebSocketFrame frame{.opcode = opcode};
    frame.payload.resize(static_cast<std::size_t>(payload_len));
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        const u8 byte = buffer[pos + i];
        frame.payload[i] = masked ? static_cast<u8>(byte ^ mask[i % 4]) : byte;
    }

    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(pos + payload_len));
    return frame;
}

void HandleParsedFrame(SSL* ssl, const ParsedWebSocketFrame& frame) {
    switch (frame.opcode) {
    case 0x1: // text
    case 0x2: // binary
        SendWebSocketFrame(ssl, 0x2, {reinterpret_cast<const u8*>(MinimalDnaResponse.data()),
                                      MinimalDnaResponse.size()});
        break;
    case 0x8: // close
        SendWebSocketFrame(ssl, 0x8, frame.payload);
        break;
    case 0x9: // ping
        SendWebSocketFrame(ssl, 0xA, frame.payload);
        break;
  default:
        break;
    }
}

void HandleDnaGatewaySession(NativeSocket client_fd) {
    LOG_INFO(Network, "DNA gateway stub: session started");

    SSL* ssl = SSL_new(g_server_ctx);
    if (!ssl) {
        CloseNativeSocket(client_fd);
        return;
    }

    SSL_set_fd(ssl, static_cast<int>(client_fd));
    if (SSL_accept(ssl) <= 0) {
        LOG_WARNING(Network, "DNA gateway stub: TLS handshake failed");
        SSL_free(ssl);
        CloseNativeSocket(client_fd);
        return;
    }

    LOG_INFO(Network, "DNA gateway stub: TLS handshake complete");

    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const int received = SSL_read(ssl, buffer, sizeof(buffer));
        if (received <= 0) {
            SSL_free(ssl);
            CloseNativeSocket(client_fd);
            return;
        }
        request.append(buffer, received);
    }

    const std::string client_key = ExtractWebSocketKey(request);
    if (client_key.empty()) {
        LOG_WARNING(Network, "DNA gateway stub: missing Sec-WebSocket-Key");
        SSL_free(ssl);
        CloseNativeSocket(client_fd);
        return;
    }

    const std::string accept = ComputeWebSocketAccept(client_key);
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept + "\r\n\r\n";
    SSL_write(ssl, response.data(), static_cast<int>(response.size()));
    LOG_INFO(Network, "DNA gateway stub: websocket upgraded");

    std::vector<u8> pending;
    while (true) {
        const int received = SSL_read(ssl, buffer, sizeof(buffer));
        if (received <= 0) {
            break;
        }
        pending.insert(pending.end(), buffer, buffer + received);

        while (true) {
            auto frame = TryParseClientFrame(pending);
            if (!frame) {
                break;
            }
            HandleParsedFrame(ssl, *frame);
            if (frame->opcode == 0x8) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                CloseNativeSocket(client_fd);
                return;
            }
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    CloseNativeSocket(client_fd);
}

void AcceptLoop() {
    const NativeSocket listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd == InvalidNativeSocket) {
        LOG_ERROR(Network, "DNA gateway stub: failed to create listener socket");
        return;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DnaGatewayPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listen_fd, 8) != 0) {
        LOG_ERROR(Network, "DNA gateway stub: failed to bind 127.0.0.1:{}", DnaGatewayPort);
        CloseNativeSocket(listen_fd);
        return;
    }

    LOG_INFO(Network, "DNA gateway stub: listening on 127.0.0.1:{}", DnaGatewayPort);

    while (true) {
        const NativeSocket client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd == InvalidNativeSocket) {
            continue;
        }
        std::thread(HandleDnaGatewaySession, client_fd).detach();
    }
}

} // namespace

bool IsDnaGatewayPort(const u16 port) {
    return port == DnaGatewayPort;
}

SockAddrIn RedirectDnaGatewayAddress(SockAddrIn addr) {
    if (!IsDnaGatewayPort(addr.portno)) {
        return addr;
    }
    EnsureDnaGatewayStubRunning();
    addr.ip = LoopbackIp;
    LOG_INFO(Network, "DNA gateway stub: redirected connect to 127.0.0.1:{}", DnaGatewayPort);
    return addr;
}

void EnsureDnaGatewayStubRunning() {
    std::lock_guard lock(g_stub_mutex);
    if (g_stub_started.exchange(true)) {
        return;
    }

    if (!InitializeServerContext()) {
        LOG_ERROR(Network, "DNA gateway stub: failed to initialize OpenSSL server context");
        g_stub_started = false;
        return;
    }

    std::thread(AcceptLoop).detach();
}

} // namespace Network
