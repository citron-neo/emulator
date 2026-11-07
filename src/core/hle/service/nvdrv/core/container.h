// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: 2022 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <memory>
#include <unordered_map>

#include "core/device_memory_manager.h"
#include "common/retained_backing_storage.h"
#include "common/producer_drain.h"
#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/service/nvdrv/nvdata.h"

namespace Kernel {
class KProcess;
}

namespace Tegra::Host1x {
class Host1x;
} // namespace Tegra::Host1x

namespace Service::Nvidia::NvCore {

class HeapMapper;
class NvMap;
class SyncpointManager;

struct ContainerImpl;

struct SessionId {
    size_t id;
    // Zero is unpublished. A reused numeric slot never reuses source identity.
    u64 generation{};
};

// Preallocated escape owner for an exception from the final Process::Close.
// Identity is diagnostic only: never reopen or retry a zero-ref failed close.
struct ProcessReleaseFailure {
    std::optional<Common::RetainedBackingStorage> backing;
    std::shared_ptr<Common::ProducerDrain> domain;
    const void* process_identity{};
    ProcessReleaseFailure* next{};
};

struct Session {
    Session(SessionId id_, Kernel::KScopedAutoObject<Kernel::KProcess>&& owner_, u64 pid_,
            Core::Asid asid_, Common::RetainedBackingStorage&& backing_,
            std::unique_ptr<ProcessReleaseFailure> failure_ = std::make_unique<ProcessReleaseFailure>());
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept = default;
    Session& operator=(Session&&);

    // Mapper and handle-dependent state must die before Process, then backing.
    std::unique_ptr<ProcessReleaseFailure> process_failure;
    std::optional<Common::RetainedBackingStorage> backing;
    Kernel::KScopedAutoObject<Kernel::KProcess> process_owner;
    SessionId id;
    Kernel::KProcess* process;
    u64 pid;
    Core::Asid asid;
    bool has_preallocated_area{};
    std::unique_ptr<HeapMapper> mapper{};
    bool is_active{};
    s32 ref_count{};
};

class Container {
public:
    struct SessionLifetime {
        SessionLifetime(Common::RetainedBackingStorage&&, Kernel::KScopedAutoObject<Kernel::KProcess>&&,
                        std::shared_ptr<Common::ProducerDrain> domain = {});
        SessionLifetime(SessionLifetime&&) noexcept = default;
        ~SessionLifetime();
        std::unique_ptr<ProcessReleaseFailure> process_failure;
        Common::RetainedBackingStorage backing;
        Kernel::KScopedAutoObject<Kernel::KProcess> process;
    };
    explicit Container(Tegra::Host1x::Host1x& host1x);
    ~Container();

    SessionId OpenSession(Kernel::KProcess* process);
    void CloseSession(SessionId id, bool is_shutdown = false);

    Session* GetSession(SessionId id);
    bool IsDisplaySessionActive(SessionId id);
    std::optional<SessionLifetime> RetainSession(SessionId id);

    NvMap& GetNvMapFile();

    const NvMap& GetNvMapFile() const;

    SyncpointManager& GetSyncpointManager();

    const SyncpointManager& GetSyncpointManager() const;

    struct Host1xDeviceFileData {
        std::unordered_map<DeviceFD, u32> fd_to_id{};
        std::deque<u32> syncpts_accumulated{};
        u32 nvdec_next_id{};
        u32 vic_next_id{};
    };

    Host1xDeviceFileData& Host1xDeviceFile();

    const Host1xDeviceFileData& Host1xDeviceFile() const;

private:
    std::unique_ptr<ContainerImpl> impl;
};

} // namespace Service::Nvidia::NvCore
