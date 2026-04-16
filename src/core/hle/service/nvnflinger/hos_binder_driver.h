// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <unordered_map>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Nvnflinger {

class HosBinderDriverServer;
class SurfaceFlinger;

class IHOSBinderDriver final : public ServiceFramework<IHOSBinderDriver> {
public:
    explicit IHOSBinderDriver(Core::System& system_, std::shared_ptr<HosBinderDriverServer> server,
                              std::shared_ptr<SurfaceFlinger> surface_flinger);
    ~IHOSBinderDriver() override;

    std::shared_ptr<SurfaceFlinger> GetSurfaceFlinger() {
        return m_surface_flinger;
    }

    std::shared_ptr<HosBinderDriverServer> GetServer() {
        return m_server;
    }

private:
    Result TransactParcel(s32 binder_id, u32 transaction_id,
                          InBuffer<BufferAttr_HipcMapAlias> parcel_data,
                          OutBuffer<BufferAttr_HipcMapAlias> parcel_reply, u32 flags);
    Result AdjustRefcount(s32 binder_id, s32 addval, s32 type);
    Result GetNativeHandle(s32 binder_id, u32 type_id,
                           OutCopyHandle<Kernel::KReadableEvent> out_handle);
    Result TransactParcelAuto(s32 binder_id, u32 transaction_id,
                              InBuffer<BufferAttr_HipcAutoSelect> parcel_data,
                              OutBuffer<BufferAttr_HipcAutoSelect> parcel_reply, u32 flags);

private:
    struct BinderRefcount {
        s32 strong{1};
        s32 weak{};
    };

    const std::shared_ptr<HosBinderDriverServer> m_server;
    const std::shared_ptr<SurfaceFlinger> m_surface_flinger;
    std::mutex m_refcount_lock;
    std::unordered_map<s32, BinderRefcount> m_refcounts;
};

} // namespace Service::Nvnflinger
