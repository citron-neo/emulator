// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>

#include "common/common_types.h"

namespace Service {

// clang-format off
template <typename T>
struct AutoOut {
    T raw;
};

template <typename T>
class Out {
public:
    using Type = T;

    /* implicit */ Out(const Out& t) : raw(t.raw) {}
    /* implicit */ Out(AutoOut<Type>& t) : raw(&t.raw) {}
    /* implicit */ Out(Type* t) : raw(t) {}
    Out& operator=(const Out&) = delete;

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    operator Type*() const {
        return raw;
    }

private:
    Type* raw;
};

template <typename T>
using SharedPointer = std::shared_ptr<T>;

template <typename T>
using OutInterface = Out<SharedPointer<T>>;

struct ClientProcessId {
    explicit operator bool() const {
        return pid != 0;
    }

    const u64& operator*() const {
        return pid;
    }

    u64 pid;
};

struct ProcessId {
    explicit ProcessId() : pid() {}
    explicit ProcessId(u64 p) : pid(p) {}
    /* implicit */ ProcessId(const ClientProcessId& c) : pid(c.pid) {}

    bool operator==(const ProcessId& rhs) const {
        return pid == rhs.pid;
    }

    explicit operator bool() const {
        return pid != 0;
    }

    const u64& operator*() const {
        return pid;
    }

    u64 pid;
};

using ClientAppletResourceUserId = ClientProcessId;
using AppletResourceUserId = ProcessId;

template <typename T>
class InCopyHandle {
public:
    using Type = T;

    /* implicit */ InCopyHandle(Type* t) : raw(t) {}
    /* implicit */ InCopyHandle() : raw() {}
    ~InCopyHandle() = default;

    InCopyHandle& operator=(Type* rhs) {
        raw = rhs;
        return *this;
    }

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    explicit operator bool() const {
        return raw != nullptr;
    }

private:
    Type* raw;
};

template <typename T>
class OutCopyHandle {
public:
    using Type = T*;

    /* implicit */ OutCopyHandle(const OutCopyHandle& t) : raw(t.raw) {}
    /* implicit */ OutCopyHandle(AutoOut<Type>& t) : raw(&t.raw) {}
    /* implicit */ OutCopyHandle(Type* t) : raw(t) {}
    OutCopyHandle& operator=(const OutCopyHandle&) = delete;

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    operator Type*() const {
        return raw;
    }

private:
    Type* raw;
};

template <typename T>
class OutMoveHandle {
public:
    using Type = T*;

    /* implicit */ OutMoveHandle(const OutMoveHandle& t) : raw(t.raw) {}
    /* implicit */ OutMoveHandle(AutoOut<Type>& t) : raw(&t.raw) {}
    /* implicit */ OutMoveHandle(Type* t) : raw(t) {}
    OutMoveHandle& operator=(const OutMoveHandle&) = delete;

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    operator Type*() const {
        return raw;
    }

private:
    Type* raw;
};

enum BufferAttr : int {
    /* 0x01 */ BufferAttr_In = (1U << 0),
    /* 0x02 */ BufferAttr_Out = (1U << 1),
    /* 0x04 */ BufferAttr_HipcMapAlias = (1U << 2),
    /* 0x08 */ BufferAttr_HipcPointer = (1U << 3),
    /* 0x10 */ BufferAttr_FixedSize = (1U << 4),
    /* 0x20 */ BufferAttr_HipcAutoSelect = (1U << 5),
    /* 0x40 */ BufferAttr_HipcMapTransferAllowsNonSecure = (1U << 6),
    /* 0x80 */ BufferAttr_HipcMapTransferAllowsNonDevice = (1U << 7),
};

template <typename T, int A>
struct Buffer : public std::span<T> {
    static_assert(std::is_trivially_copyable_v<T>, "Buffer type must be trivially copyable");
    static_assert((A & BufferAttr_FixedSize) == 0, "Buffer attr must not contain FixedSize");
    static_assert(((A & BufferAttr_In) == 0) ^ ((A & BufferAttr_Out) == 0), "Buffer attr must be In or Out");
    static constexpr BufferAttr Attr = static_cast<BufferAttr>(A);
    using Type = T;

    /* implicit */ Buffer(const std::span<T>& rhs) : std::span<T>(rhs) {}
    /* implicit */ Buffer() = default;

    Buffer& operator=(const std::span<T>& rhs) {
        std::span<T>::operator=(rhs);
        return *this;
    }

    T& operator*() const {
        return *this->data();
    }

    explicit operator bool() const {
        return this->size() > 0;
    }
};

template <int A>
using InBuffer = Buffer<const u8, BufferAttr_In | A>;

template <typename T, int A>
using InArray = Buffer<T, BufferAttr_In | A>;

template <int A>
using OutBuffer = Buffer<u8, BufferAttr_Out | A>;

template <typename T, int A>
using OutArray = Buffer<T, BufferAttr_Out | A>;

template <typename T, int A>
class InLargeData {
public:
    static_assert(std::is_trivially_copyable_v<T>, "LargeData type must be trivially copyable");
    static_assert((A & BufferAttr_Out) == 0, "InLargeData attr must not be Out");
    static constexpr BufferAttr Attr = static_cast<BufferAttr>(A | BufferAttr_In | BufferAttr_FixedSize);
    using Type = const T;

    /* implicit */ InLargeData(Type& t) : raw(&t) {}
    ~InLargeData() = default;

    InLargeData& operator=(Type* rhs) {
        raw = rhs;
        return *this;
    }

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    explicit operator bool() const {
        return raw != nullptr;
    }

private:
    Type* raw;
};

template <typename T, int A>
class OutLargeData {
public:
    static_assert(std::is_trivially_copyable_v<T>, "LargeData type must be trivially copyable");
    static_assert((A & BufferAttr_In) == 0, "OutLargeData attr must not be In");
    static constexpr BufferAttr Attr = static_cast<BufferAttr>(A | BufferAttr_Out | BufferAttr_FixedSize);
    using Type = T;

    /* implicit */ OutLargeData(const OutLargeData& t) : raw(t.raw) {}
    /* implicit */ OutLargeData(Type* t) : raw(t) {}
    /* implicit */ OutLargeData(AutoOut<T>& t) : raw(&t.raw) {}
    OutLargeData& operator=(const OutLargeData&) = delete;

    Type* Get() const {
        return raw;
    }

    Type& operator*() const {
        return *raw;
    }

    Type* operator->() const {
        return raw;
    }

    operator Type*() const {
        return raw;
    }

private:
    Type* raw;
};
// clang-format on

} // namespace Service
