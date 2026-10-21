// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/overflow.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_hardware_timer.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/svc_results.h"

namespace Kernel {
constexpr s64 DefaultTimeout = 10000000000; // 10 seconds

KResourceLimit::KResourceLimit(KernelCore& kernel)
    : KAutoObjectWithSlabHeapAndContainer{kernel}, m_lock{m_kernel}, m_cond_var{m_kernel} {}
KResourceLimit::~KResourceLimit() = default;

void KResourceLimit::Initialize() {}

void KResourceLimit::Finalize() {}

s64 KResourceLimit::GetLimitValue(LimitableResource which) const {
    const auto index = static_cast<std::size_t>(which);
    s64 value{};
    {
        KScopedLightLock lk{m_lock};
        value = m_limit_values[index];
        assert(value >= 0);
        assert(m_current_values[index] <= m_limit_values[index]);
        assert(m_current_hints[index] <= m_current_values[index]);
    }
    return value;
}

s64 KResourceLimit::GetCurrentValue(LimitableResource which) const {
    const auto index = static_cast<std::size_t>(which);
    s64 value{};
    {
        KScopedLightLock lk{m_lock};
        value = m_current_values[index];
        assert(value >= 0);
        assert(m_current_values[index] <= m_limit_values[index]);
        assert(m_current_hints[index] <= m_current_values[index]);
    }
    return value;
}

s64 KResourceLimit::GetPeakValue(LimitableResource which) const {
    const auto index = static_cast<std::size_t>(which);
    s64 value{};
    {
        KScopedLightLock lk{m_lock};
        value = m_peak_values[index];
        assert(value >= 0);
        assert(m_current_values[index] <= m_limit_values[index]);
        assert(m_current_hints[index] <= m_current_values[index]);
    }
    return value;
}

s64 KResourceLimit::GetFreeValue(LimitableResource which) const {
    const auto index = static_cast<std::size_t>(which);
    s64 value{};
    {
        KScopedLightLock lk(m_lock);
        assert(m_current_values[index] >= 0);
        assert(m_current_values[index] <= m_limit_values[index]);
        assert(m_current_hints[index] <= m_current_values[index]);
        value = m_limit_values[index] - m_current_values[index];
    }

    return value;
}

Result KResourceLimit::SetLimitValue(LimitableResource which, s64 value) {
    const auto index = static_cast<std::size_t>(which);
    KScopedLightLock lk(m_lock);
    R_UNLESS(m_current_values[index] <= value, ResultInvalidState);

    m_limit_values[index] = value;
    m_peak_values[index] = m_current_values[index];

    R_SUCCEED();
}

bool KResourceLimit::Reserve(LimitableResource which, s64 value) {
    return Reserve(which, value, m_kernel.HardwareTimer().GetTick() + DefaultTimeout);
}

bool KResourceLimit::Reserve(LimitableResource which, s64 value, s64 timeout) {
    assert(value >= 0);
    const auto index = static_cast<std::size_t>(which);
    KScopedLightLock lk(m_lock);

    assert(m_current_hints[index] <= m_current_values[index]);
    if (m_current_hints[index] >= m_limit_values[index]) {
        return false;
    }

    // Loop until we reserve or run out of time.
    while (true) {
        assert(m_current_values[index] <= m_limit_values[index]);
        assert(m_current_hints[index] <= m_current_values[index]);

        // If we would overflow, don't allow to succeed.
        if (Common::WrappingAdd(m_current_values[index], value) <= m_current_values[index]) {
            break;
        }

        if (m_current_values[index] + value <= m_limit_values[index]) {
            m_current_values[index] += value;
            m_current_hints[index] += value;
            m_peak_values[index] = std::max(m_peak_values[index], m_current_values[index]);
            return true;
        }

        if (m_current_hints[index] + value <= m_limit_values[index] &&
            (timeout < 0 || m_kernel.HardwareTimer().GetTick() < timeout)) {
            m_waiter_count++;
            m_cond_var.Wait(std::addressof(m_lock), timeout, false);
            m_waiter_count--;
        } else {
            break;
        }
    }

    return false;
}

void KResourceLimit::Release(LimitableResource which, s64 value) {
    Release(which, value, value);
}

void KResourceLimit::Release(LimitableResource which, s64 value, s64 hint) {
    assert(value >= 0);
    assert(hint >= 0);

    const auto index = static_cast<std::size_t>(which);
    KScopedLightLock lk(m_lock);
    assert(m_current_values[index] <= m_limit_values[index]);
    assert(m_current_hints[index] <= m_current_values[index]);
    assert(value <= m_current_values[index]);
    assert(hint <= m_current_hints[index]);

    m_current_values[index] -= value;
    m_current_hints[index] -= hint;

    if (m_waiter_count != 0) {
        m_cond_var.Broadcast();
    }
}

KResourceLimit* CreateResourceLimitForProcess(Core::System& system, s64 physical_memory_size) {
    auto* resource_limit = KResourceLimit::Create(system.Kernel());
    resource_limit->Initialize();

    // Initialize default resource limit values.
    // TODO(bunnei): These values are the system defaults, the limits for service processes are
    // lower. These should use the correct limit values.

    [[maybe_unused]] const auto set_limit_value_result =
        resource_limit->SetLimitValue(LimitableResource::PhysicalMemoryMax, physical_memory_size);
    assert(set_limit_value_result.IsSuccess());
    [[maybe_unused]] const auto set_limit_value_result_2 =
        resource_limit->SetLimitValue(LimitableResource::ThreadCountMax, 800);
    assert(set_limit_value_result_2.IsSuccess());
    [[maybe_unused]] const auto set_limit_value_result_3 =
        resource_limit->SetLimitValue(LimitableResource::EventCountMax, 900);
    assert(set_limit_value_result_3.IsSuccess());
    [[maybe_unused]] const auto set_limit_value_result_4 =
        resource_limit->SetLimitValue(LimitableResource::TransferMemoryCountMax, 200);
    assert(set_limit_value_result_4.IsSuccess());
    [[maybe_unused]] const auto set_limit_value_result_5 =
        resource_limit->SetLimitValue(LimitableResource::SessionCountMax, 1133);
    assert(set_limit_value_result_5.IsSuccess());

    return resource_limit;
}

} // namespace Kernel
