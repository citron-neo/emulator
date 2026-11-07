// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <exception>
#include <utility>

#include <fmt/format.h>
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec_common.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvjpg.h"
#include "core/hle/service/nvdrv/devices/nvhost_vic.h"
#include "core/hle/service/nvdrv/devices/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/nvmemp.h"
#include "core/hle/service/nvnflinger/nvnflinger.h"
#include "core/hle/service/server_manager.h"
#include "video_core/gpu.h"

// [UNITY-FIX] undef Win32 macros shadowing ServiceContext methods.
#undef CreateEvent
#undef CreateMutex
#undef CreateSemaphore

namespace Service::Nvidia {

EventInterface::EventInterface(Module& module_) : module{module_}, guard{}, on_signal{} {}

EventInterface::~EventInterface() = default;

Kernel::KEvent* EventInterface::CreateEvent(std::string name) {
    Kernel::KEvent* new_event = module.service_context.CreateEvent(std::move(name));
    return new_event;
}

void EventInterface::FreeEvent(Kernel::KEvent* event) {
    module.service_context.CloseEvent(event);
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>(system);
    const auto NvdrvInterfaceFactoryForApplication = [&, module] {
        return std::make_shared<NVDRV>(system, module, "nvdrv");
    };
    const auto NvdrvInterfaceFactoryForApplets = [&, module] {
        return std::make_shared<NVDRV>(system, module, "nvdrv:a");
    };
    const auto NvdrvInterfaceFactoryForSysmodules = [&, module] {
        return std::make_shared<NVDRV>(system, module, "nvdrv:s");
    };
    const auto NvdrvInterfaceFactoryForTesting = [&, module] {
        return std::make_shared<NVDRV>(system, module, "nvdrv:t");
    };
    server_manager->RegisterNamedService("nvdrv", NvdrvInterfaceFactoryForApplication);
    server_manager->RegisterNamedService("nvdrv:a", NvdrvInterfaceFactoryForApplets);
    server_manager->RegisterNamedService("nvdrv:s", NvdrvInterfaceFactoryForSysmodules);
    server_manager->RegisterNamedService("nvdrv:t", NvdrvInterfaceFactoryForTesting);
    server_manager->RegisterNamedService("nvmemp", std::make_shared<NVMEMP>(system));
    ServerManager::RunServer(std::move(server_manager));
}

Module::Module(Core::System& system)
    : dispatch_domain{system.Kernel().NvdrvDispatchDomain()},
      retirements{std::make_shared<Common::OwnedRetirementQueue>(dispatch_domain)},
      system{system}, container{system.Host1x()},
      service_context{system, "nvdrv"}, events_interface{*this} {
    builders["/dev/nvhost-as-gpu"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_as_gpu>(system, *this, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-gpu"] = [this, &system](DeviceFD fd) {
        auto allocation = system.GPU().AllocateChannel();
        if (!allocation) return OpenResult{open_files.end()};
        auto device = std::make_shared<Devices::nvhost_gpu>(system, events_interface, container,
                                                         std::move(allocation->channel));
        return OpenResult{open_files.emplace(fd, std::move(device)).first,
                          std::move(allocation->publication)};
    };
    builders["/dev/nvhost-ctrl-gpu"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_ctrl_gpu>(system, events_interface);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvmap"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvmap>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvdisp_disp0"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvdisp_disp0>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-ctrl"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_ctrl>(system, events_interface, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-nvdec"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_nvdec>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-nvjpg"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_nvjpg>(system);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-vic"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_vic>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
}

Module::~Module() {
    // Devices release events and syncpoints through these still-live providers.
    open_files.clear();
}

std::optional<Module::SessionRetirement> Module::PrepareSessionRetirement(
    Kernel::KProcess* process, const Common::ProducerDrain::Entry&) {
    if (!retirements->Drain()) return {};
    auto retained = Kernel::KScopedAutoObject<Kernel::KProcess>::TryOpen(process);
    auto backing = system.DeviceMemory().buffer.RetainBackingStorage();
    if (!retained || !backing || !backing->OwnerAlive()) return {};
    auto lifetime = std::make_shared<NvCore::Container::SessionLifetime>(
        NvCore::Container::SessionLifetime{std::move(*backing), std::move(*retained), dispatch_domain});
    auto published = std::make_shared<std::optional<NvCore::SessionId>>();
    auto record = Common::OwnedRetirementQueue::Prepare(
        [owner = shared_from_this(), lifetime, published] {
            if (!lifetime->backing.OwnerAlive() || !*published) return false;
            owner->container.CloseSession(**published, true);
            return true;
        });
    return SessionRetirement{std::move(published), std::move(record)};
}

std::shared_ptr<NvCore::Container::SessionLifetime> Module::RetainProcessLifetime(
    Kernel::KProcess* process, const Common::ProducerDrain::Entry&) {
    auto retained = Kernel::KScopedAutoObject<Kernel::KProcess>::TryOpen(process);
    auto backing = system.DeviceMemory().buffer.RetainBackingStorage();
    if (!retained || !backing || !backing->OwnerAlive()) return {};
    return std::make_shared<NvCore::Container::SessionLifetime>(
        std::move(*backing), std::move(*retained), dispatch_domain);
}

Common::OwnedRetirementQueue::Owner Module::PrepareGraphicBufferRetirement(
    u32 id, const Common::ProducerDrain::Entry&) {
    if (!retirements->Drain()) return {};
    auto handle = container.GetNvMapFile().GetHandle(id);
    if (!handle) return {};
    NvCore::SessionId session_id;
    {
        std::scoped_lock lock{handle->mutex};
        if (!handle->allocated) return {};
        session_id = handle->session_id;
    }
    auto retained = container.RetainSession(session_id);
    if (!retained) return {};
    auto lifetime = std::make_shared<NvCore::Container::SessionLifetime>(std::move(*retained));
    auto record = Common::OwnedRetirementQueue::Prepare(
        [owner = shared_from_this(), handle, lifetime] {
            if (!lifetime->backing.OwnerAlive()) return false;
            auto& file = owner->container.GetNvMapFile();
            file.UnpinHandle(handle->id);
            return file.FreeHandle(handle->id, true).has_value();
        });
    try {
        container.GetNvMapFile().DuplicateHandle(id, true);
        if (!container.GetNvMapFile().PinHandle(id, false)) {
            retirements->Quarantine(std::move(record));
            return {};
        }
    } catch (...) {
        retirements->Quarantine(std::move(record));
        return {};
    }
    return record;
}

NvResult Module::VerifyFD(DeviceFD fd) const {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    if (open_files.find(fd) == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return NvResult::Success;
}

DeviceFD Module::Open(const std::string& device_name, NvCore::SessionId session_id) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return INVALID_NVDRV_FD;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return INVALID_NVDRV_FD;
    auto it = builders.find(device_name);
    if (it == builders.end()) {
        LOG_ERROR(Service_NVDRV, "Trying to open unknown device {}", device_name);
        return INVALID_NVDRV_FD;
    }

    const DeviceFD fd = next_fd++;
    auto& builder = it->second;
    auto opened = builder(fd);
    if (opened.file == open_files.end()) return INVALID_NVDRV_FD;
    auto device = opened.file->second;

    device->OnOpen(session_id, fd);

    return fd;
}

NvResult Module::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                        std::span<u8> output) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return NvResult::Busy;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return NvResult::Busy;
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl1(fd, command, input, output);
}

NvResult Module::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                        std::span<const u8> inline_input, std::span<u8> output) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return NvResult::Busy;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return NvResult::Busy;
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl2(fd, command, input, inline_input, output);
}

NvResult Module::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output,
                        std::span<u8> inline_output) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return NvResult::Busy;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return NvResult::Busy;
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl3(fd, command, input, output, inline_output);
}

NvResult Module::Close(DeviceFD fd) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return NvResult::Busy;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return NvResult::Busy;
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    itr->second->OnClose(fd);

    open_files.erase(itr);

    return NvResult::Success;
}

NvResult Module::QueryEvent(DeviceFD fd, u32 event_id, Kernel::KEvent*& event) {
    auto dispatch = dispatch_domain->EnterCold();
    if (!dispatch) return NvResult::Busy;
    const auto exception_depth = std::uncaught_exceptions();
    SCOPE_EXIT {
        if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();
    };
    if (!retirements->Drain()) return NvResult::Busy;
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    event = itr->second->QueryEvent(event_id);
    if (!event) {
        return NvResult::BadParameter;
    }
    return NvResult::Success;
}

} // namespace Service::Nvidia
