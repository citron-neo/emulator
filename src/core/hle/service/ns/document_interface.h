// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDocumentInterface final : public ServiceFramework<IDocumentInterface> {
public:
    explicit IDocumentInterface(Core::System& system_);
    ~IDocumentInterface() override;

private:
    Result ResolveApplicationContentPath(ContentPath content_path);
    Result GetRunningApplicationProgramId(Out<u64> out_program_id, u64 caller_program_id);
};

} // namespace Service::NS
