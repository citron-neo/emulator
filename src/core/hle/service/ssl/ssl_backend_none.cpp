// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/internal_network/network.h"
#include "core/hle/service/ssl/ssl_backend.h"

namespace Service::SSL {

class SSLConnectionBackendNone final : public SSLConnectionBackend {
public:
    SSLConnectionBackendNone() = default;
    ~SSLConnectionBackendNone() = default;

    void SetSocket(std::shared_ptr<Network::SocketBase> socket_) override {
        socket = std::move(socket_);
    }

    Result SetHostName(const std::string& hostname) override {
        LOG_WARNING(Service_SSL, "(STUBBED) SetHostName hostname={}", hostname);
        return ResultSuccess;
    }

    Result DoHandshake() override {
        LOG_WARNING(Service_SSL, "(STUBBED) Pretending to do TLS handshake");
        return ResultSuccess;
    }

    Result Read(size_t* out_size, std::span<u8> buffer) override {
        LOG_WARNING(Service_SSL, "(STUBBED) Read called, using raw socket");
        if (!socket) {
            return ResultNoSocket;
        }

        // Just pass through to the socket directly (no TLS)
#ifdef __ANDROID__
        // On Android, we need to handle this differently
        *out_size = 0;
        LOG_WARNING(Service_SSL, "(STUBBED) Read not implemented on Android");
        return ResultSuccess;
#else
        Network::Errno recv_result = socket->Recv(buffer.data(), buffer.size(), 0, *out_size);
        if (recv_result == Network::Errno::AGAIN) {
            return ResultWouldBlock;
        } else if (recv_result != Network::Errno::SUCCESS) {
            LOG_ERROR(Service_SSL, "Error during socket read: {}", static_cast<int>(recv_result));
            return ResultInternalError;
        }
#endif

        return ResultSuccess;
    }

    Result Write(size_t* out_size, std::span<const u8> data) override {
        LOG_WARNING(Service_SSL, "(STUBBED) Write called, using raw socket");
        if (!socket) {
            return ResultNoSocket;
        }

        // Just pass through to the socket directly (no TLS)
#ifdef __ANDROID__
        // On Android, we need to handle this differently
        *out_size = 0;
        LOG_WARNING(Service_SSL, "(STUBBED) Write not implemented on Android");
        return ResultSuccess;
#else
        Network::Errno send_result = socket->Send(data.data(), data.size(), 0, *out_size);
        if (send_result == Network::Errno::AGAIN) {
            return ResultWouldBlock;
        } else if (send_result != Network::Errno::SUCCESS) {
            LOG_ERROR(Service_SSL, "Error during socket write: {}", static_cast<int>(send_result));
            return ResultInternalError;
        }
#endif

        return ResultSuccess;
    }

    Result GetServerCerts(std::vector<std::vector<u8>>* out_certs) override {
        LOG_WARNING(Service_SSL, "(STUBBED) GetServerCerts called");
        // Return an empty certificate to prevent crashes
        out_certs->emplace_back(std::vector<u8>{0x30, 0x82, 0x01, 0x01}); // Minimal dummy DER certificate header
        return ResultSuccess;
    }

private:
    std::shared_ptr<Network::SocketBase> socket;
};

Result CreateSSLConnectionBackend(std::unique_ptr<SSLConnectionBackend>* out_backend) {
    LOG_WARNING(Service_SSL, "Creating stub SSL backend (no real TLS encryption)");
    *out_backend = std::make_unique<SSLConnectionBackendNone>();
    return ResultSuccess;
}

} // namespace Service::SSL
