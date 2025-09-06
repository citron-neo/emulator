// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service {

namespace KernelHelpers {
class ServiceContext;
}

class Event {
public:
    explicit Event(KernelHelpers::ServiceContext& ctx);
    ~Event();

    void Signal();
    void Clear();

    Kernel::KReadableEvent* GetHandle();

private:
    Kernel::KEvent* m_event;
};

} // namespace Service
