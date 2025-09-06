// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDynamicRightsInterface;
class IReadOnlyApplicationControlDataInterface;
class IReadOnlyApplicationRecordInterface;
class IECommerceInterface;
class IApplicationVersionInterface;
class IFactoryResetInterface;
class IAccountProxyInterface;
class IApplicationManagerInterface;
class IDownloadTaskInterface;
class IContentManagementInterface;
class IDocumentInterface;

class IServiceGetterInterface : public ServiceFramework<IServiceGetterInterface> {
public:
    explicit IServiceGetterInterface(Core::System& system_, const char* name);
    ~IServiceGetterInterface() override;

public:
    Result GetDynamicRightsInterface(Out<SharedPointer<IDynamicRightsInterface>> out_interface);
    Result GetReadOnlyApplicationControlDataInterface(
        Out<SharedPointer<IReadOnlyApplicationControlDataInterface>> out_interface);
    Result GetReadOnlyApplicationRecordInterface(
        Out<SharedPointer<IReadOnlyApplicationRecordInterface>> out_interface);
    Result GetECommerceInterface(Out<SharedPointer<IECommerceInterface>> out_interface);
    Result GetApplicationVersionInterface(
        Out<SharedPointer<IApplicationVersionInterface>> out_interface);
    Result GetFactoryResetInterface(Out<SharedPointer<IFactoryResetInterface>> out_interface);
    Result GetAccountProxyInterface(Out<SharedPointer<IAccountProxyInterface>> out_interface);
    Result GetApplicationManagerInterface(
        Out<SharedPointer<IApplicationManagerInterface>> out_interface);
    Result GetDownloadTaskInterface(Out<SharedPointer<IDownloadTaskInterface>> out_interface);
    Result GetContentManagementInterface(
        Out<SharedPointer<IContentManagementInterface>> out_interface);
    Result GetDocumentInterface(Out<SharedPointer<IDocumentInterface>> out_interface);
};

} // namespace Service::NS
