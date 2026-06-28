// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/internal_network/dna_gateway_stub.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <limits>
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

// Offline discovery: route every Artemis backend through the local DNA gateway stub.
constexpr std::string_view DiscoveryServicesResponse =
    R"([{"serviceId":"47e3073624bf47668f96f88f8d307b17","name":"sso","baseUrl":"https://508223012e5a5ff19f30a391b2bdadc0.my.2k.com/sso/v2.0","tags":["public"],"scheme":"https","host":"508223012e5a5ff19f30a391b2bdadc0.my.2k.com","contextPath":"/sso/v2.0"},{"serviceId":"03311b6047f74b218b2c3271c12ad242","name":"promotions","baseUrl":"https://508223012e5a5ff19f30a391b2bdadc0.my.2k.com/promotions/api/v1/","tags":["public"],"scheme":"https","host":"508223012e5a5ff19f30a391b2bdadc0.my.2k.com","contextPath":"/promotions/api/v1/"},{"serviceId":"f118276174e14165bfc87eb75f93d30e","name":"telemetry","baseUrl":"https://508223012e5a5ff19f30a391b2bdadc0.my.2k.com/telemetry/v2","tags":["public"],"scheme":"https","host":"508223012e5a5ff19f30a391b2bdadc0.my.2k.com","contextPath":"/telemetry/v2"},{"serviceId":"97d6892ea7e448ed9aeec8e4a0cd99c8","name":"entitlements","baseUrl":"https://508223012e5a5ff19f30a391b2bdadc0.my.2k.com/entitlements/v2.0","tags":["public"],"scheme":"https","host":"508223012e5a5ff19f30a391b2bdadc0.my.2k.com","contextPath":"/entitlements/v2.0"},{"serviceId":"a3fc6770f34241188674e232b3fa7323","name":"discovery","baseUrl":"https://508223012e5a5ff19f30a391b2bdadc0.my.2k.com/discovery/v1","tags":["public"],"scheme":"https","host":"508223012e5a5ff19f30a391b2bdadc0.my.2k.com","contextPath":"/discovery/v1"}])";

std::mutex g_stub_mutex;
std::atomic<bool> g_stub_started{false};
std::atomic<bool> g_listener_ready{false};
std::condition_variable g_listener_cv;
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

    static const unsigned char alpn_http11[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    SSL_CTX_set_alpn_protos(g_server_ctx, alpn_http11, sizeof(alpn_http11));

    if (!GenerateSelfSignedCertificate(g_server_ctx)) {
        SSL_CTX_free(g_server_ctx);
        g_server_ctx = nullptr;
        return false;
    }
    return true;
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

std::size_t FindHttpHeadersEnd(const std::string& request) {
    const auto crlf = request.find("\r\n\r\n");
    if (crlf != std::string::npos) {
        return crlf + 4;
    }
    const auto lf = request.find("\n\n");
    if (lf != std::string::npos) {
        return lf + 2;
    }
    return std::string::npos;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string TrimAscii(std::string value) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string ExtractHeaderValue(const std::string& request, std::string_view header_name) {
    const std::string lower_request = ToLowerAscii(request);
    const std::string lower_name = ToLowerAscii(std::string(header_name));
    const std::string needle = lower_name + ":";
    const auto pos = lower_request.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + needle.size();
    const auto crlf_end = lower_request.find("\r\n", start);
    const auto lf_end = lower_request.find('\n', start);
    const auto end = [&] {
        if (crlf_end == std::string::npos) {
            return lf_end;
        }
        if (lf_end == std::string::npos) {
            return crlf_end;
        }
        return std::min(crlf_end, lf_end);
    }();
    if (end == std::string::npos) {
        return {};
    }
    return TrimAscii(request.substr(start, end - start));
}

std::optional<std::size_t> ParseContentLength(const std::string& request) {
    const std::string value = ExtractHeaderValue(request, "Content-Length");
    if (value.empty()) {
        return 0;
    }
    try {
        const unsigned long long length = std::stoull(value);
        if (length > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(length);
    } catch (...) {
        return std::nullopt;
    }
}

bool ConsumeHttpRequestBody(SSL* ssl, std::string& buffer, const std::string& request,
                            char* read_buffer, std::size_t read_buffer_size) {
    const auto content_length = ParseContentLength(request);
    if (!content_length) {
        return false;
    }
    while (buffer.size() < *content_length) {
        const int received = SSL_read(ssl, read_buffer, static_cast<int>(read_buffer_size));
        if (received <= 0) {
            return false;
        }
        buffer.append(read_buffer, received);
    }
    if (*content_length > 0) {
        buffer.erase(0, *content_length);
    }
    return true;
}

std::string ExtractWebSocketKey(const std::string& request) {
    return ExtractHeaderValue(request, "Sec-WebSocket-Key");
}

std::string ExtractWebSocketProtocol(const std::string& request) {
    return ExtractHeaderValue(request, "Sec-WebSocket-Protocol");
}

bool RequestWantsWebSocketUpgrade(const std::string& request) {
    const std::string connection = ToLowerAscii(ExtractHeaderValue(request, "Connection"));
    const std::string upgrade = ToLowerAscii(ExtractHeaderValue(request, "Upgrade"));
    return connection.find("upgrade") != std::string::npos && upgrade == "websocket";
}

bool IsHttp2ConnectionPreface(std::string_view request) {
    return request.starts_with("PRI * HTTP/2.0");
}

constexpr std::string_view Http2ConnectionPreface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

bool BufferStartsWithHttp2Preface(std::string_view buffer) {
    return buffer.starts_with("PRI * HTTP/2.0");
}

bool TryConsumeHttp2Preface(std::string& buffer) {
    if (buffer.size() >= Http2ConnectionPreface.size() &&
        buffer.compare(0, Http2ConnectionPreface.size(), Http2ConnectionPreface) == 0) {
        buffer.erase(0, Http2ConnectionPreface.size());
        LOG_INFO(Network, "DNA gateway stub: consumed HTTP/2 connection preface");
        return true;
    }
    return false;
}

std::string ExtractRequestPath(std::string_view request_line) {
    const auto method_end = request_line.find(' ');
    if (method_end == std::string_view::npos) {
        return {};
    }
    const auto path_start = method_end + 1;
    const auto path_end = request_line.find(' ', path_start);
    if (path_end == std::string_view::npos) {
        return {};
    }
    std::string path(request_line.substr(path_start, path_end - path_start));
    if (const auto query = path.find('?'); query != std::string::npos) {
        path.erase(query);
    }
    return path;
}

std::string BuildHttpResponse(int status_code, std::string_view status_text, std::string_view body,
                              std::string_view extra_headers = {}) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + std::string(status_text) +
                           "\r\nContent-Type: application/json\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\nConnection: keep-alive\r\n";
    if (!extra_headers.empty()) {
        response += std::string(extra_headers);
        if (extra_headers.back() != '\n') {
            response += "\r\n";
        }
    }
    response += "\r\n";
    response += std::string(body);
    return response;
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

    static constexpr u64 kMaxWebSocketPayload = 1 << 20;
    if (payload_len > kMaxWebSocketPayload) {
        return std::nullopt;
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

bool SendPlainHttpResponse(SSL* ssl, int status_code, std::string_view status_text,
                           std::string_view body, std::string_view extra_headers = {}) {
    const std::string response = BuildHttpResponse(status_code, status_text, body, extra_headers);
    return SSL_write(ssl, response.data(), static_cast<int>(response.size())) > 0;
}

bool TryUpgradeWebSocket(SSL* ssl, const std::string& request) {
    const std::string client_key = ExtractWebSocketKey(request);
    if (client_key.empty()) {
        return false;
    }

    const std::string accept = ComputeWebSocketAccept(client_key);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept + "\r\n";
    const std::string protocol = ExtractWebSocketProtocol(request);
    if (!protocol.empty()) {
        response += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
    }
    response += "\r\n";
    if (SSL_write(ssl, response.data(), static_cast<int>(response.size())) <= 0) {
        return false;
    }
    LOG_INFO(Network, "DNA gateway stub: websocket upgraded");
    return true;
}

bool HandlePlainHttpRequest(SSL* ssl, const std::string& request) {
    const auto first_line_end = request.find('\n');
    const std::string first_line =
        first_line_end == std::string::npos ? request : request.substr(0, first_line_end);
    const std::string path = ExtractRequestPath(TrimAscii(first_line));
    LOG_INFO(Network, "DNA gateway stub: plain HTTP request: {}",
             TrimAscii(first_line));

    if (path.starts_with("/discovery/v1/services")) {
        constexpr std::string_view discovery_headers =
            "X-2k-Result-Total: 5\r\n"
            "X-2k-Result-Count: 5\r\n"
            "X-2k-Result-More: false\r\n";
        if (!SendPlainHttpResponse(ssl, 200, "OK", DiscoveryServicesResponse, discovery_headers)) {
            return false;
        }
        LOG_INFO(Network, "DNA gateway stub: sent discovery services response");
        return true;
    }

    if (path.starts_with("/telemetry/") || path.starts_with("/sso/") ||
        path.starts_with("/entitlements/") || path.starts_with("/promotions/")) {
        if (!SendPlainHttpResponse(ssl, 200, "OK", MinimalDnaResponse)) {
            return false;
        }
        LOG_INFO(Network, "DNA gateway stub: sent offline API response for {}", path);
        return true;
    }

    if (!SendPlainHttpResponse(ssl, 200, "OK", MinimalDnaResponse)) {
        return false;
    }
    LOG_INFO(Network, "DNA gateway stub: sent plain HTTP JSON response");
    return true;
}

void ServiceWebSocketFrames(SSL* ssl) {
    std::vector<u8> pending;
    char buffer[4096];
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
                return;
            }
        }
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

    std::string buffer;
    char read_buffer[4096];
    bool websocket_upgraded = false;

    while (true) {
        if (BufferStartsWithHttp2Preface(buffer)) {
            if (buffer.size() < Http2ConnectionPreface.size()) {
                const int received = SSL_read(ssl, read_buffer, sizeof(read_buffer));
                if (received <= 0) {
                    SSL_free(ssl);
                    CloseNativeSocket(client_fd);
                    return;
                }
                buffer.append(read_buffer, received);
                continue;
            }
            if (TryConsumeHttp2Preface(buffer)) {
                continue;
            }
        }

        while (FindHttpHeadersEnd(buffer) == std::string::npos) {
            const int received = SSL_read(ssl, read_buffer, sizeof(read_buffer));
            if (received <= 0) {
                SSL_free(ssl);
                CloseNativeSocket(client_fd);
                return;
            }
            buffer.append(read_buffer, received);
        }

        const std::size_t headers_end = FindHttpHeadersEnd(buffer);
        const std::string request = buffer.substr(0, headers_end);
        buffer.erase(0, headers_end);

        if (IsHttp2ConnectionPreface(request) || TrimAscii(request) == "SM") {
            LOG_INFO(Network, "DNA gateway stub: skipping HTTP/2 preface fragment");
            continue;
        }

        if (RequestWantsWebSocketUpgrade(request)) {
            if (TryUpgradeWebSocket(ssl, request)) {
                websocket_upgraded = true;
                break;
            }
            LOG_WARNING(Network, "DNA gateway stub: websocket upgrade requested but key missing");
            LOG_WARNING(Network, "DNA gateway stub: request headers:\n{}",
                        request.substr(0, std::min(request.size(), std::size_t{1024})));
            if (!HandlePlainHttpRequest(ssl, request)) {
                SSL_free(ssl);
                CloseNativeSocket(client_fd);
                return;
            }
            if (!ConsumeHttpRequestBody(ssl, buffer, request, read_buffer, sizeof(read_buffer))) {
                SSL_free(ssl);
                CloseNativeSocket(client_fd);
                return;
            }
            continue;
        }

        if (!HandlePlainHttpRequest(ssl, request)) {
            SSL_free(ssl);
            CloseNativeSocket(client_fd);
            return;
        }
        if (!ConsumeHttpRequestBody(ssl, buffer, request, read_buffer, sizeof(read_buffer))) {
            SSL_free(ssl);
            CloseNativeSocket(client_fd);
            return;
        }
    }

    if (websocket_upgraded) {
        if (!buffer.empty()) {
            std::vector<u8> pending(buffer.begin(), buffer.end());
            char ws_buffer[4096];
            while (true) {
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

                const int received = SSL_read(ssl, ws_buffer, sizeof(ws_buffer));
                if (received <= 0) {
                    break;
                }
                pending.insert(pending.end(), ws_buffer, ws_buffer + received);
            }
        } else {
            ServiceWebSocketFrames(ssl);
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
        g_stub_started.store(false);
        g_listener_ready.store(false);
        return;
    }

    g_listener_ready.store(true, std::memory_order_release);
    g_listener_cv.notify_all();
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

bool IsLikelyMy2kGatewayHost(const std::array<u8, 4>& ip) {
    // Observed *.my.2k.com A records in LEGO 2K Drive sessions.
    static constexpr std::array<std::array<u8, 4>, 2> KnownHosts{{
        {3, 149, 117, 250},
        {3, 151, 148, 249},
    }};
    return std::any_of(KnownHosts.begin(), KnownHosts.end(),
                       [&ip](const auto& known) { return ip == known; });
}

bool ShouldRedirectToDnaGatewayStub(const SockAddrIn& addr) {
    if (addr.portno == DnaGatewayPort) {
        return true;
    }
    return addr.portno == DnaGatewayHttpsPort &&
           (IsLikelyMy2kGatewayHost(addr.ip) || addr.ip == LoopbackIp);
}

bool IsDnaGatewayPort(const u16 port) {
    return port == DnaGatewayPort || port == DnaGatewayHttpsPort;
}

SockAddrIn RedirectDnaGatewayAddress(SockAddrIn addr) {
    if (!ShouldRedirectToDnaGatewayStub(addr)) {
        return addr;
    }
    EnsureDnaGatewayStubRunning();
    const u16 original_port = addr.portno;
    const std::string original_ip = IPv4AddressToString(addr.ip);
    addr.ip = LoopbackIp;
    addr.portno = DnaGatewayPort;
    LOG_INFO(Network,
             "DNA gateway stub: redirected connect from {}:{} to 127.0.0.1:{}",
             original_ip, original_port, DnaGatewayPort);
    return addr;
}

void EnsureDnaGatewayStubRunning() {
    {
        std::lock_guard lock(g_stub_mutex);
        if (g_stub_started.exchange(true)) {
            // Another thread is already starting or has started the stub.
        } else if (!InitializeServerContext()) {
            LOG_ERROR(Network, "DNA gateway stub: failed to initialize OpenSSL server context");
            g_stub_started = false;
            return;
        } else {
            std::thread(AcceptLoop).detach();
        }
    }

    if (g_listener_ready.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock lock(g_stub_mutex);
    g_listener_cv.wait_for(lock, std::chrono::seconds(2), [] {
        return g_listener_ready.load(std::memory_order_acquire);
    });
}

} // namespace Network
