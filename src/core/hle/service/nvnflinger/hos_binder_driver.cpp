// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/nvnflinger/binder.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"

namespace Service::Nvnflinger {

IHOSBinderDriver::IHOSBinderDriver(Core::System& system_,
                                   std::shared_ptr<HosBinderDriverServer> server,
                                   std::shared_ptr<SurfaceFlinger> surface_flinger)
    : ServiceFramework{system_, "IHOSBinderDriver"}, m_server(server),
      m_surface_flinger(surface_flinger) {
    static const FunctionInfo functions[] = {
        {0, C<&IHOSBinderDriver::TransactParcel>, "TransactParcel"},
        {1, C<&IHOSBinderDriver::AdjustRefcount>, "AdjustRefcount"},
        {2, C<&IHOSBinderDriver::GetNativeHandle>, "GetNativeHandle"},
        {3, C<&IHOSBinderDriver::TransactParcelAuto>, "TransactParcelAuto"},
    };
    RegisterHandlers(functions);
}

IHOSBinderDriver::~IHOSBinderDriver() = default;

Result IHOSBinderDriver::TransactParcel(s32 binder_id, u32 transaction_id,
                                        InBuffer<BufferAttr_HipcMapAlias> parcel_data,
                                        OutBuffer<BufferAttr_HipcMapAlias> parcel_reply,
                                        u32 flags) {
    LOG_DEBUG(Service_VI, "called. id={} transaction={}, flags={}", binder_id, transaction_id,
              flags);

    const auto binder = m_server->TryGetBinder(binder_id);
    R_SUCCEED_IF(binder == nullptr);

    binder->Transact(transaction_id, parcel_data, parcel_reply, flags);

    R_SUCCEED();
}

Result IHOSBinderDriver::AdjustRefcount(s32 binder_id, s32 addval, s32 type) {
    LOG_WARNING(Service_VI, "(STUBBED) called id={}, addval={}, type={}", binder_id, addval, type);
    R_SUCCEED();
}

Result IHOSBinderDriver::GetNativeHandle(s32 binder_id, u32 type_id,
                                         OutCopyHandle<Kernel::KReadableEvent> out_handle) {
    LOG_WARNING(Service_VI, "(STUBBED) called id={}, type_id={}", binder_id, type_id);

    const auto binder = m_server->TryGetBinder(binder_id);
    R_UNLESS(binder != nullptr, ResultUnknown);

    *out_handle = binder->GetNativeHandle(type_id);

    R_SUCCEED();
}

Result IHOSBinderDriver::TransactParcelAuto(s32 binder_id, u32 transaction_id,
                                            InBuffer<BufferAttr_HipcAutoSelect> parcel_data,
                                            OutBuffer<BufferAttr_HipcAutoSelect> parcel_reply,
                                            u32 flags) {
    R_RETURN(this->TransactParcel(binder_id, transaction_id, parcel_data, parcel_reply, flags));
}

} // namespace Service::Nvnflinger
