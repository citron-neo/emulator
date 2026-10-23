// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <mach/mach.h>
#include <mach/message.h>

#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <bit>

#include <fmt/format.h>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/backend/exception_handler.h"

#if defined(ARCHITECTURE_x86_64)

#    include "dynarmic/backend/x64/block_of_code.h"
#    define mig_external extern "C"
#    include "dynarmic/backend/x64/mig/mach_exc_server.h"

#    define THREAD_STATE x86_THREAD_STATE64
#    define THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT

using dynarmic_thread_state_t = x86_thread_state64_t;

#elif defined(ARCHITECTURE_arm64)

#    include <oaknut/code_block.hpp>
#    define mig_external extern "C"
#    include "dynarmic/backend/arm64/mig/mach_exc_server.h"

#    define THREAD_STATE ARM_THREAD_STATE64
#    define THREAD_STATE_COUNT ARM_THREAD_STATE64_COUNT

using dynarmic_thread_state_t = arm_thread_state64_t;

#endif

namespace Dynarmic::Backend {

namespace {

struct CodeBlockInfo {
    u64 code_begin, code_end;
    std::function<FakeCall(u64)> cb;
};

struct MachMessage {
    mach_msg_header_t head;
    char data[2048];  ///< Arbitrary size
};

class MachHandler final {
public:
    MachHandler();
    ~MachHandler();

    kern_return_t HandleRequest(dynarmic_thread_state_t* thread_state);

    void AddCodeBlock(CodeBlockInfo info);
    void RemoveCodeBlock(u64 rip);

private:
    auto FindCodeBlockInfo(u64 rip) {
        return std::find_if(code_block_infos.begin(), code_block_infos.end(), [&](const auto& x) { return x.code_begin <= rip && x.code_end > rip; });
    }

    std::vector<CodeBlockInfo> code_block_infos;
    std::mutex code_block_infos_mutex;

    std::thread thread;
    mach_port_t server_port;

    void MessagePump();
};

MachHandler::MachHandler() {
#define KCHECK(x) ASSERT((x) == KERN_SUCCESS && "init failure at " #x)
    KCHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &server_port));
    KCHECK(mach_port_insert_right(mach_task_self(), server_port, server_port, MACH_MSG_TYPE_MAKE_SEND));
    KCHECK(task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, server_port, EXCEPTION_STATE | MACH_EXCEPTION_CODES, THREAD_STATE));
    // The below doesn't actually work, and I'm not sure why; since this doesn't work we'll have a spurious
    // error message upon shutdown.
    mach_port_t prev;
    KCHECK(mach_port_request_notification(mach_task_self(), server_port, MACH_NOTIFY_PORT_DESTROYED, 0, server_port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev));
#undef KCHECK

    thread = std::thread(&MachHandler::MessagePump, this);
    thread.detach();
}

MachHandler::~MachHandler() {
    mach_port_deallocate(mach_task_self(), server_port);
}

void MachHandler::MessagePump() {
    mach_msg_return_t mr;
    MachMessage request;
    MachMessage reply;

    while (true) {
        mr = mach_msg(&request.head, MACH_RCV_MSG | MACH_RCV_LARGE, 0, sizeof(request), server_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (mr != MACH_MSG_SUCCESS) {
            fmt::print(stderr, "dynarmic: macOS MachHandler: Failed to receive mach message. error: {:#08x} ({})\n", mr, mach_error_string(mr));
            return;
        }

        if (!mach_exc_server(&request.head, &reply.head)) {
            fmt::print(stderr, "dynarmic: macOS MachHandler: Unexpected mach message\n");
            return;
        }

        mr = mach_msg(&reply.head, MACH_SEND_MSG, reply.head.msgh_size, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (mr != MACH_MSG_SUCCESS) {
            fmt::print(stderr, "dynarmic: macOS MachHandler: Failed to send mach message. error: {:#08x} ({})\n", mr, mach_error_string(mr));
            return;
        }
    }
}

#if defined(ARCHITECTURE_x86_64)
kern_return_t MachHandler::HandleRequest(x86_thread_state64_t* ts) {
    std::lock_guard<std::mutex> guard(code_block_infos_mutex);

    const auto iter = FindCodeBlockInfo(ts->__rip);
    if (iter == code_block_infos.end()) {
        fmt::print(stderr, "Unhandled EXC_BAD_ACCESS at rip {:#016x}\n", ts->__rip);
        return KERN_FAILURE;
    }

    FakeCall fc = iter->cb(ts->__rip);

    ts->__rsp -= sizeof(u64);
    *std::bit_cast<u64*>(ts->__rsp) = fc.ret_rip;
    ts->__rip = fc.call_rip;

    return KERN_SUCCESS;
}
#elif defined(ARCHITECTURE_arm64)
kern_return_t MachHandler::HandleRequest(arm_thread_state64_t* ts) {
    std::lock_guard<std::mutex> guard(code_block_infos_mutex);

    const auto iter = FindCodeBlockInfo(ts->__pc);
    if (iter == code_block_infos.end()) {
        fmt::print(stderr, "Unhandled EXC_BAD_ACCESS at pc {:#016x}\n", ts->__pc);
        return KERN_FAILURE;
    }

    FakeCall fc = iter->cb(ts->__pc);

    // TODO: Sign with ptrauth_sign_unauthenticated if pointer authentication is enabled.
    ts->__pc = fc.call_pc;

    return KERN_SUCCESS;
}
#endif

void MachHandler::AddCodeBlock(CodeBlockInfo cbi) {
    std::lock_guard<std::mutex> guard(code_block_infos_mutex);
    if (auto iter = FindCodeBlockInfo(cbi.code_begin); iter != code_block_infos.end()) {
        code_block_infos.erase(iter);
    }
    code_block_infos.push_back(cbi);
}

void MachHandler::RemoveCodeBlock(u64 rip) {
    std::lock_guard<std::mutex> guard(code_block_infos_mutex);
    const auto iter = FindCodeBlockInfo(rip);
    if (iter == code_block_infos.end()) {
        return;
    }
    code_block_infos.erase(iter);
}

std::mutex handler_lock;
std::optional<MachHandler> mach_handler;

void RegisterHandler() {
    std::lock_guard<std::mutex> guard(handler_lock);
    if (!mach_handler) {
        mach_handler.emplace();
    }
}

}  // anonymous namespace

mig_external kern_return_t catch_mach_exception_raise(mach_port_t, mach_port_t, mach_port_t, exception_type_t, mach_exception_data_t, mach_msg_type_number_t) {
    fmt::print(stderr, "dynarmic: Unexpected mach message: mach_exception_raise\n");
    return KERN_FAILURE;
}

mig_external kern_return_t catch_mach_exception_raise_state_identity(mach_port_t, mach_port_t, mach_port_t, exception_type_t, mach_exception_data_t, mach_msg_type_number_t, int*, thread_state_t, mach_msg_type_number_t, thread_state_t, mach_msg_type_number_t*) {
    fmt::print(stderr, "dynarmic: Unexpected mach message: mach_exception_raise_state_identity\n");
    return KERN_FAILURE;
}

mig_external kern_return_t catch_mach_exception_raise_state(
    mach_port_t /*exception_port*/,
    exception_type_t exception,
    const mach_exception_data_t /*code*/,  // code[0] is as per kern_return.h, code[1] is rip.
    mach_msg_type_number_t /*codeCnt*/,
    int* flavor,
    const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t* new_stateCnt) {
    if (!flavor || !new_stateCnt) {
        fmt::print(stderr, "dynarmic: catch_mach_exception_raise_state: Invalid arguments.\n");
        return KERN_INVALID_ARGUMENT;
    }
    if (*flavor != THREAD_STATE || old_stateCnt != THREAD_STATE_COUNT || *new_stateCnt < THREAD_STATE_COUNT) {
        fmt::print(stderr, "dynarmic: catch_mach_exception_raise_state: Unexpected flavor.\n");
        return KERN_INVALID_ARGUMENT;
    }
    if (exception != EXC_BAD_ACCESS) {
        fmt::print(stderr, "dynarmic: catch_mach_exception_raise_state: Unexpected exception type.\n");
        return KERN_FAILURE;
    }

    // The input/output pointers are not necessarily 8-byte aligned.
    dynarmic_thread_state_t ts;
    std::memcpy(&ts, old_state, sizeof(ts));

    kern_return_t ret = mach_handler->HandleRequest(&ts);

    std::memcpy(new_state, &ts, sizeof(ts));
    *new_stateCnt = THREAD_STATE_COUNT;
    return ret;
}

struct ExceptionHandler::Impl final {
    Impl(u64 code_begin_, u64 code_end_)
            : code_begin(code_begin_)
            , code_end(code_end_) {
        RegisterHandler();
    }

    void SetCallback(std::function<FakeCall(u64)> cb) {
        CodeBlockInfo cbi;
        cbi.code_begin = code_begin;
        cbi.code_end = code_end;
        cbi.cb = cb;
        mach_handler->AddCodeBlock(cbi);
    }

    ~Impl() {
        mach_handler->RemoveCodeBlock(code_begin);
    }

private:
    u64 code_begin, code_end;
};

ExceptionHandler::ExceptionHandler() = default;
ExceptionHandler::~ExceptionHandler() = default;

#if defined(ARCHITECTURE_x86_64)
void ExceptionHandler::Register(X64::BlockOfCode& code) {
    const u64 code_begin = std::bit_cast<u64>(code.getCode());
    const u64 code_end = code_begin + code.GetTotalCodeSize();
    impl = std::make_unique<Impl>(code_begin, code_end);
}
#elif defined(ARCHITECTURE_arm64)
void ExceptionHandler::Register(oaknut::CodeBlock& mem, std::size_t size) {
    const u64 code_begin = std::bit_cast<u64>(mem.ptr());
    const u64 code_end = code_begin + size;
    impl = std::make_unique<Impl>(code_begin, code_end);
}
#else
#    error "Invalid architecture"
#endif

bool ExceptionHandler::SupportsFastmem() const noexcept {
    return static_cast<bool>(impl);
}

void ExceptionHandler::SetFastmemCallback(std::function<FakeCall(u64)> cb) {
    impl->SetCallback(cb);
}

}  // namespace Dynarmic::Backend
