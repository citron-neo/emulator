// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

class IDisplayController final : public ServiceFramework<IDisplayController> {
public:
    explicit IDisplayController(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IDisplayController() override;

private:
    Result GetCallerAppletCaptureImageEx(Out<bool> out_was_written,
                                         OutBuffer<BufferAttr_HipcMapAlias> out_image_data);
    Result TakeScreenShotOfOwnLayer(bool unknown0, s32 fbshare_layer_index);
    Result ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color);
    Result AcquireLastForegroundCaptureSharedBuffer(Out<bool> out_was_written,
                                                    Out<s32> out_fbshare_layer_index);
    Result ReleaseLastForegroundCaptureSharedBuffer();
    Result AcquireCallerAppletCaptureSharedBuffer(Out<bool> out_was_written,
                                                  Out<s32> out_fbshare_layer_index);
    Result ReleaseCallerAppletCaptureSharedBuffer();
    Result AcquireLastApplicationCaptureSharedBuffer(Out<bool> out_was_written,
                                                     Out<s32> out_fbshare_layer_index);
    Result ReleaseLastApplicationCaptureSharedBuffer();

    const std::shared_ptr<Applet> applet;
};

} // namespace Service::AM
