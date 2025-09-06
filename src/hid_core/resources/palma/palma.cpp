// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/kernel_helpers.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/palma/palma.h"

namespace Service::HID {

Palma::Palma(Core::HID::HIDCore& hid_core_, KernelHelpers::ServiceContext& service_context_)
    : ControllerBase{hid_core_}, service_context{service_context_} {
    controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Other);
    operation_complete_event = service_context.CreateEvent("hid:PalmaOperationCompleteEvent");
}

Palma::~Palma() {
    service_context.CloseEvent(operation_complete_event);
};

void Palma::OnInit() {}

void Palma::OnRelease() {}

void Palma::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    if (!IsControllerActivated()) {
        return;
    }
}

Result Palma::GetPalmaConnectionHandle(Core::HID::NpadIdType npad_id,
                                       PalmaConnectionHandle& handle) {
    active_handle.npad_id = npad_id;
    handle = active_handle;
    return ResultSuccess;
}

Result Palma::InitializePalma(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    Activate();
    return ResultSuccess;
}

Kernel::KReadableEvent& Palma::AcquirePalmaOperationCompleteEvent(
    const PalmaConnectionHandle& handle) const {
    if (handle.npad_id != active_handle.npad_id) {
        LOG_ERROR(Service_HID, "Invalid npad id {}", handle.npad_id);
    }
    return operation_complete_event->GetReadableEvent();
}

Result Palma::GetPalmaOperationInfo(const PalmaConnectionHandle& handle,
                                    PalmaOperationType& operation_type,
                                    std::span<u8> out_data) const {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation_type = static_cast<PalmaOperationType>(operation.operation);
    std::memcpy(out_data.data(), operation.data.data(),
                std::min(out_data.size(), operation.data.size()));

    return ResultSuccess;
}

Result Palma::PlayPalmaActivity(const PalmaConnectionHandle& handle, u64 palma_activity) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::PlayActivity;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::SetPalmaFrModeType(const PalmaConnectionHandle& handle, PalmaFrModeType fr_mode_) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    fr_mode = fr_mode_;
    return ResultSuccess;
}

Result Palma::ReadPalmaStep(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::ReadStep;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::EnablePalmaStep(const PalmaConnectionHandle& handle, bool is_enabled) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    return ResultSuccess;
}

Result Palma::ResetPalmaStep(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    return ResultSuccess;
}

void Palma::ReadPalmaApplicationSection() {}

void Palma::WritePalmaApplicationSection() {}

Result Palma::ReadPalmaUniqueCode(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::ReadUniqueCode;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::SetPalmaUniqueCodeInvalid(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::SetUniqueCodeInvalid;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

void Palma::WritePalmaActivityEntry() {}

Result Palma::WritePalmaRgbLedPatternEntry(const PalmaConnectionHandle& handle, u64 unknown) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::WriteRgbLedPatternEntry;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::WritePalmaWaveEntry(const PalmaConnectionHandle& handle, PalmaWaveSet wave,
                                  Common::ProcessAddress t_mem, u64 size) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::WriteWaveEntry;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::SetPalmaDataBaseIdentificationVersion(const PalmaConnectionHandle& handle,
                                                    s32 database_id_version_) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    database_id_version = database_id_version_;
    operation.operation = PackedPalmaOperationType::ReadDataBaseIdentificationVersion;
    operation.result = PalmaResultSuccess;
    operation.data[0] = {};
    operation_complete_event->Signal();
    return ResultSuccess;
}

Result Palma::GetPalmaDataBaseIdentificationVersion(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    operation.operation = PackedPalmaOperationType::ReadDataBaseIdentificationVersion;
    operation.result = PalmaResultSuccess;
    operation.data = {};
    operation.data[0] = static_cast<u8>(database_id_version);
    operation_complete_event->Signal();
    return ResultSuccess;
}

void Palma::SuspendPalmaFeature() {}

Result Palma::GetPalmaOperationResult(const PalmaConnectionHandle& handle) const {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    return operation.result;
}
void Palma::ReadPalmaPlayLog() {}

void Palma::ResetPalmaPlayLog() {}

void Palma::SetIsPalmaAllConnectable(bool is_all_connectable) {
    // If true controllers are able to be paired
    is_connectable = is_all_connectable;
}

void Palma::SetIsPalmaPairedConnectable() {}

Result Palma::PairPalma(const PalmaConnectionHandle& handle) {
    if (handle.npad_id != active_handle.npad_id) {
        return InvalidPalmaHandle;
    }
    // TODO: Do something
    return ResultSuccess;
}

void Palma::SetPalmaBoostMode(bool boost_mode) {}

void Palma::CancelWritePalmaWaveEntry() {}

void Palma::EnablePalmaBoostMode() {}

void Palma::GetPalmaBluetoothAddress() {}

void Palma::SetDisallowedPalmaConnection() {}

} // namespace Service::HID
