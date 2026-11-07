// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

#include "common/common_types.h"
#include "common/producer_drain.h"
#include "common/owned_retirement.h"
#include "common/terminal_ingress.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
class KProcess;
}

namespace Service::Nvidia {

namespace NvCore {
class Container;
class SyncpointManager;
} // namespace NvCore

namespace Devices {
class nvdevice;
class nvhost_ctrl;
} // namespace Devices

class Module;

class EventInterface {
public:
    explicit EventInterface(Module& module_);
    ~EventInterface();

    Kernel::KEvent* CreateEvent(std::string name);

    void FreeEvent(Kernel::KEvent* event);

private:
    Module& module;
    std::mutex guard;
    std::list<Devices::nvhost_ctrl*> on_signal;
};

class Module final : public std::enable_shared_from_this<Module> {
public:
    explicit Module(Core::System& system_);
    ~Module();

    /// Returns a pointer to one of the available devices, identified by its name.
    template <typename T>
    std::shared_ptr<T> GetDevice(DeviceFD fd) {
        auto itr = open_files.find(fd);
        if (itr == open_files.end())
            return nullptr;
        return std::static_pointer_cast<T>(itr->second);
    }

    NvResult VerifyFD(DeviceFD fd) const;

    /// Opens a device node and returns a file descriptor to it.
    DeviceFD Open(const std::string& device_name, NvCore::SessionId session_id);

    /// Sends an ioctl command to the specified file descriptor.
    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output);

    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output);

    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output,
                    std::span<u8> inline_output);

    /// Closes a device file descriptor and returns operation success.
    NvResult Close(DeviceFD fd);

    NvResult QueryEvent(DeviceFD fd, u32 event_id, Kernel::KEvent*& event);

    struct SessionRetirement {
        std::shared_ptr<std::optional<NvCore::SessionId>> published;
        Common::OwnedRetirementQueue::Owner record;
    };
    auto TryEnterDispatch() const { return dispatch_domain->EnterCold(); }
    // An admitted direct device operation may already have changed its prefix.
    // Seal before its entry exits; ordinary returned errors need not quarantine.
    void QuarantineDispatch() const noexcept { dispatch_domain->Seal(); }
    std::shared_ptr<NvCore::Container::SessionLifetime> RetainProcessLifetime(
        Kernel::KProcess* process, const Common::ProducerDrain::Entry&);
    std::optional<SessionRetirement> PrepareSessionRetirement(Kernel::KProcess* process,
                                               const Common::ProducerDrain::Entry&);
    Common::OwnedRetirementQueue::Owner PrepareGraphicBufferRetirement(
        u32 handle, const Common::ProducerDrain::Entry&);
    std::shared_ptr<Common::OwnedRetirementQueue> RetirementQueue() const { return retirements; }

    NvCore::Container& GetContainer() {
        return container;
    }

private:
    friend class EventInterface;

    const std::shared_ptr<Common::ProducerDrain> dispatch_domain;
    std::shared_ptr<Common::OwnedRetirementQueue> retirements;
    Core::System& system;

    /// Manages syncpoints on the host
    NvCore::Container container;

    /// Id to use for the next open file descriptor.
    DeviceFD next_fd = 1;

    using FilesContainerType = std::unordered_map<DeviceFD, std::shared_ptr<Devices::nvdevice>>;
    /// Mapping of file descriptors to the devices they reference.
    FilesContainerType open_files;

    KernelHelpers::ServiceContext service_context;

    EventInterface events_interface;

    struct OpenResult {
        FilesContainerType::iterator file;
        std::optional<Common::TerminalIngress::Entry> publication;
        OpenResult(FilesContainerType::iterator file_) : file{file_} {}
        OpenResult(FilesContainerType::iterator file_, Common::TerminalIngress::Entry&& entry)
            : file{file_}, publication{std::move(entry)} {}
    };
    std::unordered_map<std::string, std::function<OpenResult(DeviceFD)>> builders;
};

void LoopProcess(Core::System& system);

} // namespace Service::Nvidia
