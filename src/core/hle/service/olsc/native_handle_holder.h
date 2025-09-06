// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::OLSC {

class INativeHandleHolder final : public ServiceFramework<INativeHandleHolder> {
public:
    explicit INativeHandleHolder(Core::System& system_);
    ~INativeHandleHolder() override;

private:
    Result GetNativeHandle(OutCopyHandle<Kernel::KReadableEvent> out_event);
};

} // namespace Service::OLSC
