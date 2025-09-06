// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/os/multi_wait.h"

namespace Service {

namespace impl {

class AutoMultiWaitHolder {
private:
    MultiWaitHolder m_holder;

public:
    template <typename T>
    explicit AutoMultiWaitHolder(MultiWait* multi_wait, T&& arg) : m_holder(arg) {
        m_holder.LinkToMultiWait(multi_wait);
    }

    ~AutoMultiWaitHolder() {
        m_holder.UnlinkFromMultiWait();
    }

    std::pair<MultiWaitHolder*, int> ConvertResult(const std::pair<MultiWaitHolder*, int> result,
                                                   int index) {
        if (result.first == std::addressof(m_holder)) {
            return std::make_pair(static_cast<MultiWaitHolder*>(nullptr), index);
        } else {
            return result;
        }
    }
};

using WaitAnyFunction = decltype(&MultiWait::WaitAny);

inline std::pair<MultiWaitHolder*, int> WaitAnyImpl(Kernel::KernelCore& kernel,
                                                    MultiWait* multi_wait, WaitAnyFunction func,
                                                    int) {
    return std::pair<MultiWaitHolder*, int>((multi_wait->*func)(kernel), -1);
}

template <typename T, typename... Args>
inline std::pair<MultiWaitHolder*, int> WaitAnyImpl(Kernel::KernelCore& kernel,
                                                    MultiWait* multi_wait, WaitAnyFunction func,
                                                    int index, T&& x, Args&&... args) {
    AutoMultiWaitHolder holder(multi_wait, std::forward<T>(x));
    return holder.ConvertResult(
        WaitAnyImpl(kernel, multi_wait, func, index + 1, std::forward<Args>(args)...), index);
}

template <typename... Args>
inline std::pair<MultiWaitHolder*, int> WaitAnyImpl(Kernel::KernelCore& kernel,
                                                    MultiWait* multi_wait, WaitAnyFunction func,
                                                    Args&&... args) {
    return WaitAnyImpl(kernel, multi_wait, func, 0, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::pair<MultiWaitHolder*, int> WaitAnyImpl(Kernel::KernelCore& kernel,
                                                    WaitAnyFunction func, Args&&... args) {
    MultiWait temp_multi_wait;
    return WaitAnyImpl(kernel, std::addressof(temp_multi_wait), func, 0,
                       std::forward<Args>(args)...);
}

class NotBoolButInt {
public:
    constexpr NotBoolButInt(int v) : m_value(v) {}
    constexpr operator int() const {
        return m_value;
    }
    explicit operator bool() const = delete;

private:
    int m_value;
};

} // namespace impl

template <typename... Args>
    requires(sizeof...(Args) > 0)
inline std::pair<MultiWaitHolder*, int> WaitAny(Kernel::KernelCore& kernel, MultiWait* multi_wait,
                                                Args&&... args) {
    return impl::WaitAnyImpl(kernel, &MultiWait::WaitAny, multi_wait, std::forward<Args>(args)...);
}

template <typename... Args>
    requires(sizeof...(Args) > 0)
inline int WaitAny(Kernel::KernelCore& kernel, Args&&... args) {
    return impl::WaitAnyImpl(kernel, &MultiWait::WaitAny, std::forward<Args>(args)...).second;
}

template <typename... Args>
    requires(sizeof...(Args) > 0)
inline std::pair<MultiWaitHolder*, int> TryWaitAny(Kernel::KernelCore& kernel,
                                                   MultiWait* multi_wait, Args&&... args) {
    return impl::WaitAnyImpl(kernel, &MultiWait::TryWaitAny, multi_wait,
                             std::forward<Args>(args)...);
}

template <typename... Args>
    requires(sizeof...(Args) > 0)
inline impl::NotBoolButInt TryWaitAny(Kernel::KernelCore& kernel, Args&&... args) {
    return impl::WaitAnyImpl(kernel, &MultiWait::TryWaitAny, std::forward<Args>(args)...).second;
}

} // namespace Service
