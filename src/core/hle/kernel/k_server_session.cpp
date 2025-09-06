// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <tuple>
#include <utility>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/scratch_buffer.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_client_port.h"
#include "core/hle/kernel/k_handle_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/hle/kernel/k_server_port.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/k_thread_queue.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/message_buffer.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/memory.h"

namespace Kernel {

namespace {

constexpr inline size_t PointerTransferBufferAlignment = 0x10;
constexpr inline size_t ReceiveListDataSize =
    MessageBuffer::MessageHeader::ReceiveListCountType_CountMax *
    MessageBuffer::ReceiveListEntry::GetDataSize() / sizeof(u32);

using ThreadQueueImplForKServerSessionRequest = KThreadQueue;

class ReceiveList {
public:
    static constexpr int GetEntryCount(const MessageBuffer::MessageHeader& header) {
        const auto count = header.GetReceiveListCount();
        switch (count) {
        case MessageBuffer::MessageHeader::ReceiveListCountType_None:
            return 0;
        case MessageBuffer::MessageHeader::ReceiveListCountType_ToMessageBuffer:
            return 0;
        case MessageBuffer::MessageHeader::ReceiveListCountType_ToSingleBuffer:
            return 1;
        default:
            return count - MessageBuffer::MessageHeader::ReceiveListCountType_CountOffset;
        }
    }

    explicit ReceiveList(const u32* dst_msg, uint64_t dst_address,
                         KProcessPageTable& dst_page_table,
                         const MessageBuffer::MessageHeader& dst_header,
                         const MessageBuffer::SpecialHeader& dst_special_header, size_t msg_size,
                         size_t out_offset, s32 dst_recv_list_idx, bool is_tls) {
        m_recv_list_count = dst_header.GetReceiveListCount();
        m_msg_buffer_end = dst_address + sizeof(u32) * out_offset;
        m_msg_buffer_space_end = dst_address + msg_size;

        // NOTE: Nintendo calculates the receive list index here using the special header.
        // We pre-calculate it in the caller, and pass it as a parameter.
        (void)dst_special_header;

        const u32* recv_list = dst_msg + dst_recv_list_idx;
        const auto entry_count = GetEntryCount(dst_header);

        if (is_tls) {
            // Messages from TLS to TLS are contained within one page.
            std::memcpy(m_data.data(), recv_list,
                        entry_count * MessageBuffer::ReceiveListEntry::GetDataSize());
        } else {
            // If any buffer is not from TLS, perform a normal read instead.
            uint64_t cur_addr = dst_address + dst_recv_list_idx * sizeof(u32);
            dst_page_table.GetMemory().ReadBlock(
                cur_addr, m_data.data(),
                entry_count * MessageBuffer::ReceiveListEntry::GetDataSize());
        }
    }

    bool IsIndex() const {
        return m_recv_list_count >
               static_cast<s32>(MessageBuffer::MessageHeader::ReceiveListCountType_CountOffset);
    }

    bool IsToMessageBuffer() const {
        return m_recv_list_count ==
               MessageBuffer::MessageHeader::ReceiveListCountType_ToMessageBuffer;
    }

    void GetBuffer(uint64_t& out, size_t size, int& key) const {
        switch (m_recv_list_count) {
        case MessageBuffer::MessageHeader::ReceiveListCountType_None: {
            out = 0;
            break;
        }
        case MessageBuffer::MessageHeader::ReceiveListCountType_ToMessageBuffer: {
            const uint64_t buf =
                Common::AlignUp(m_msg_buffer_end + key, PointerTransferBufferAlignment);

            if ((buf < buf + size) && (buf + size <= m_msg_buffer_space_end)) {
                out = buf;
                key = static_cast<int>(buf + size - m_msg_buffer_end);
            } else {
                out = 0;
            }
            break;
        }
        case MessageBuffer::MessageHeader::ReceiveListCountType_ToSingleBuffer: {
            const MessageBuffer::ReceiveListEntry entry(m_data[0], m_data[1]);
            const uint64_t buf =
                Common::AlignUp(entry.GetAddress() + key, PointerTransferBufferAlignment);

            const uint64_t entry_addr = entry.GetAddress();
            const size_t entry_size = entry.GetSize();

            if ((buf < buf + size) && (entry_addr < entry_addr + entry_size) &&
                (buf + size <= entry_addr + entry_size)) {
                out = buf;
                key = static_cast<int>(buf + size - entry_addr);
            } else {
                out = 0;
            }
            break;
        }
        default: {
            if (key < m_recv_list_count -
                          static_cast<s32>(
                              MessageBuffer::MessageHeader::ReceiveListCountType_CountOffset)) {
                const MessageBuffer::ReceiveListEntry entry(m_data[2 * key + 0],
                                                            m_data[2 * key + 1]);

                const uintptr_t entry_addr = entry.GetAddress();
                const size_t entry_size = entry.GetSize();

                if ((entry_addr < entry_addr + entry_size) && (entry_size >= size)) {
                    out = entry_addr;
                }
            } else {
                out = 0;
            }
            break;
        }
        }
    }

private:
    std::array<u32, ReceiveListDataSize> m_data;
    s32 m_recv_list_count;
    uint64_t m_msg_buffer_end;
    uint64_t m_msg_buffer_space_end;
};

template <bool MoveHandleAllowed>
Result ProcessMessageSpecialData(s32& offset, KProcess& dst_process, KProcess& src_process,
                                 KThread& src_thread, const MessageBuffer& dst_msg,
                                 const MessageBuffer& src_msg,
                                 const MessageBuffer::SpecialHeader& src_special_header) {
    // Copy the special header to the destination.
    offset = dst_msg.Set(src_special_header);

    // Copy the process ID.
    if (src_special_header.GetHasProcessId()) {
        offset = dst_msg.SetProcessId(offset, src_process.GetProcessId());
    }

    // Prepare to process handles.
    auto& dst_handle_table = dst_process.GetHandleTable();
    auto& src_handle_table = src_process.GetHandleTable();
    Result result = ResultSuccess;

    // Process copy handles.
    for (auto i = 0; i < src_special_header.GetCopyHandleCount(); ++i) {
        // Get the handles.
        const Handle src_handle = src_msg.GetHandle(offset);
        Handle dst_handle = Svc::InvalidHandle;

        // If we're in a success state, try to move the handle to the new table.
        if (R_SUCCEEDED(result) && src_handle != Svc::InvalidHandle) {
            KScopedAutoObject obj =
                src_handle_table.GetObjectForIpc(src_handle, std::addressof(src_thread));
            if (obj.IsNotNull()) {
                Result add_result =
                    dst_handle_table.Add(std::addressof(dst_handle), obj.GetPointerUnsafe());
                if (R_FAILED(add_result)) {
                    result = add_result;
                    dst_handle = Svc::InvalidHandle;
                }
            } else {
                result = ResultInvalidHandle;
            }
        }

        // Set the handle.
        offset = dst_msg.SetHandle(offset, dst_handle);
    }

    // Process move handles.
    if constexpr (MoveHandleAllowed) {
        for (auto i = 0; i < src_special_header.GetMoveHandleCount(); ++i) {
            // Get the handles.
            const Handle src_handle = src_msg.GetHandle(offset);
            Handle dst_handle = Svc::InvalidHandle;

            // Whether or not we've succeeded, we need to remove the handles from the source table.
            if (src_handle != Svc::InvalidHandle) {
                if (R_SUCCEEDED(result)) {
                    KScopedAutoObject obj =
                        src_handle_table.GetObjectForIpcWithoutPseudoHandle(src_handle);
                    if (obj.IsNotNull()) {
                        Result add_result = dst_handle_table.Add(std::addressof(dst_handle),
                                                                 obj.GetPointerUnsafe());

                        src_handle_table.Remove(src_handle);

                        if (R_FAILED(add_result)) {
                            result = add_result;
                            dst_handle = Svc::InvalidHandle;
                        }
                    } else {
                        result = ResultInvalidHandle;
                    }
                } else {
                    src_handle_table.Remove(src_handle);
                }
            }

            // Set the handle.
            offset = dst_msg.SetHandle(offset, dst_handle);
        }
    }

    R_RETURN(result);
}

Result ProcessReceiveMessagePointerDescriptors(int& offset, int& pointer_key,
                                               KProcessPageTable& dst_page_table,
                                               KProcessPageTable& src_page_table,
                                               const MessageBuffer& dst_msg,
                                               const MessageBuffer& src_msg,
                                               const ReceiveList& dst_recv_list, bool dst_user) {
    // Get the offset at the start of processing.
    const int cur_offset = offset;

    // Get the pointer desc.
    MessageBuffer::PointerDescriptor src_desc(src_msg, cur_offset);
    offset += static_cast<int>(MessageBuffer::PointerDescriptor::GetDataSize() / sizeof(u32));

    // Extract address/size.
    const uint64_t src_pointer = src_desc.GetAddress();
    const size_t recv_size = src_desc.GetSize();
    uint64_t recv_pointer = 0;

    // Process the buffer, if it has a size.
    if (recv_size > 0) {
        // If using indexing, set index.
        if (dst_recv_list.IsIndex()) {
            pointer_key = src_desc.GetIndex();
        }

        // Get the buffer.
        dst_recv_list.GetBuffer(recv_pointer, recv_size, pointer_key);
        R_UNLESS(recv_pointer != 0, ResultOutOfResource);

        // Perform the pointer data copy.
        if (dst_user) {
            R_TRY(src_page_table.CopyMemoryFromHeapToHeapWithoutCheckDestination(
                dst_page_table, recv_pointer, recv_size, KMemoryState::FlagReferenceCounted,
                KMemoryState::FlagReferenceCounted,
                KMemoryPermission::NotMapped | KMemoryPermission::KernelReadWrite,
                KMemoryAttribute::Uncached | KMemoryAttribute::Locked, KMemoryAttribute::Locked,
                src_pointer, KMemoryState::FlagLinearMapped, KMemoryState::FlagLinearMapped,
                KMemoryPermission::UserRead, KMemoryAttribute::Uncached, KMemoryAttribute::None));
        } else {
            R_TRY(src_page_table.CopyMemoryFromLinearToUser(
                recv_pointer, recv_size, src_pointer, KMemoryState::FlagLinearMapped,
                KMemoryState::FlagLinearMapped, KMemoryPermission::UserRead,
                KMemoryAttribute::Uncached, KMemoryAttribute::None));
        }
    }

    // Set the output descriptor.
    dst_msg.Set(cur_offset, MessageBuffer::PointerDescriptor(reinterpret_cast<void*>(recv_pointer),
                                                             recv_size, src_desc.GetIndex()));

    R_SUCCEED();
}

constexpr Result GetMapAliasMemoryState(KMemoryState& out,
                                        MessageBuffer::MapAliasDescriptor::Attribute attr) {
    switch (attr) {
    case MessageBuffer::MapAliasDescriptor::Attribute::Ipc:
        out = KMemoryState::Ipc;
        break;
    case MessageBuffer::MapAliasDescriptor::Attribute::NonSecureIpc:
        out = KMemoryState::NonSecureIpc;
        break;
    case MessageBuffer::MapAliasDescriptor::Attribute::NonDeviceIpc:
        out = KMemoryState::NonDeviceIpc;
        break;
    default:
        R_THROW(ResultInvalidCombination);
    }

    R_SUCCEED();
}

constexpr Result GetMapAliasTestStateAndAttributeMask(KMemoryState& out_state,
                                                      KMemoryAttribute& out_attr_mask,
                                                      KMemoryState state) {
    switch (state) {
    case KMemoryState::Ipc:
        out_state = KMemoryState::FlagCanUseIpc;
        out_attr_mask =
            KMemoryAttribute::Uncached | KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonSecureIpc:
        out_state = KMemoryState::FlagCanUseNonSecureIpc;
        out_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonDeviceIpc:
        out_state = KMemoryState::FlagCanUseNonDeviceIpc;
        out_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    default:
        R_THROW(ResultInvalidCombination);
    }

    R_SUCCEED();
}

void CleanupSpecialData(KProcess& dst_process, u32* dst_msg_ptr, size_t dst_buffer_size) {
    // Parse the message.
    const MessageBuffer dst_msg(dst_msg_ptr, dst_buffer_size);
    const MessageBuffer::MessageHeader dst_header(dst_msg);
    const MessageBuffer::SpecialHeader dst_special_header(dst_msg, dst_header);

    // Check that the size is big enough.
    if (MessageBuffer::GetMessageBufferSize(dst_header, dst_special_header) > dst_buffer_size) {
        return;
    }

    // Set the special header.
    int offset = dst_msg.Set(dst_special_header);

    // Clear the process id, if needed.
    if (dst_special_header.GetHasProcessId()) {
        offset = dst_msg.SetProcessId(offset, 0);
    }

    // Clear handles, as relevant.
    auto& dst_handle_table = dst_process.GetHandleTable();
    for (auto i = 0;
         i < (dst_special_header.GetCopyHandleCount() + dst_special_header.GetMoveHandleCount());
         ++i) {
        const Handle handle = dst_msg.GetHandle(offset);

        if (handle != Svc::InvalidHandle) {
            dst_handle_table.Remove(handle);
        }

        offset = dst_msg.SetHandle(offset, Svc::InvalidHandle);
    }
}

Result CleanupServerHandles(KernelCore& kernel, uint64_t message, size_t buffer_size,
                            KPhysicalAddress message_paddr) {
    // Server is assumed to be current thread.
    KThread& thread = GetCurrentThread(kernel);

    // Get the linear message pointer.
    u32* msg_ptr;
    if (message) {
        msg_ptr = kernel.System().DeviceMemory().GetPointer<u32>(message_paddr);
    } else {
        msg_ptr = GetCurrentMemory(kernel).GetPointer<u32>(thread.GetTlsAddress());
        buffer_size = MessageBufferSize;
        message = GetInteger(thread.GetTlsAddress());
    }

    // Parse the message.
    const MessageBuffer msg(msg_ptr, buffer_size);
    const MessageBuffer::MessageHeader header(msg);
    const MessageBuffer::SpecialHeader special_header(msg, header);

    // Check that the size is big enough.
    R_UNLESS(MessageBuffer::GetMessageBufferSize(header, special_header) <= buffer_size,
             ResultInvalidCombination);

    // If there's a special header, there may be move handles we need to close.
    if (header.GetHasSpecialHeader()) {
        // Determine the offset to the start of handles.
        auto offset = msg.GetSpecialDataIndex(header, special_header);
        if (special_header.GetHasProcessId()) {
            offset += static_cast<int>(sizeof(u64) / sizeof(u32));
        }
        if (auto copy_count = special_header.GetCopyHandleCount(); copy_count > 0) {
            offset += static_cast<int>((sizeof(Svc::Handle) * copy_count) / sizeof(u32));
        }

        // Get the handle table.
        auto& handle_table = thread.GetOwnerProcess()->GetHandleTable();

        // Close the handles.
        for (auto i = 0; i < special_header.GetMoveHandleCount(); ++i) {
            handle_table.Remove(msg.GetHandle(offset));
            offset += static_cast<int>(sizeof(Svc::Handle) / sizeof(u32));
        }
    }

    R_SUCCEED();
}

Result CleanupServerMap(KSessionRequest* request, KProcess* server_process) {
    // If there's no server process, there's nothing to clean up.
    R_SUCCEED_IF(server_process == nullptr);

    // Get the page table.
    auto& server_page_table = server_process->GetPageTable();

    // Cleanup Send mappings.
    for (size_t i = 0; i < request->GetSendCount(); ++i) {
        R_TRY(server_page_table.CleanupForIpcServer(request->GetSendServerAddress(i),
                                                    request->GetSendSize(i),
                                                    request->GetSendMemoryState(i)));
    }

    // Cleanup Receive mappings.
    for (size_t i = 0; i < request->GetReceiveCount(); ++i) {
        R_TRY(server_page_table.CleanupForIpcServer(request->GetReceiveServerAddress(i),
                                                    request->GetReceiveSize(i),
                                                    request->GetReceiveMemoryState(i)));
    }

    // Cleanup Exchange mappings.
    for (size_t i = 0; i < request->GetExchangeCount(); ++i) {
        R_TRY(server_page_table.CleanupForIpcServer(request->GetExchangeServerAddress(i),
                                                    request->GetExchangeSize(i),
                                                    request->GetExchangeMemoryState(i)));
    }

    R_SUCCEED();
}

Result CleanupClientMap(KSessionRequest* request, KProcessPageTable* client_page_table) {
    // If there's no client page table, there's nothing to clean up.
    R_SUCCEED_IF(client_page_table == nullptr);

    // Cleanup Send mappings.
    for (size_t i = 0; i < request->GetSendCount(); ++i) {
        R_TRY(client_page_table->CleanupForIpcClient(request->GetSendClientAddress(i),
                                                     request->GetSendSize(i),
                                                     request->GetSendMemoryState(i)));
    }

    // Cleanup Receive mappings.
    for (size_t i = 0; i < request->GetReceiveCount(); ++i) {
        R_TRY(client_page_table->CleanupForIpcClient(request->GetReceiveClientAddress(i),
                                                     request->GetReceiveSize(i),
                                                     request->GetReceiveMemoryState(i)));
    }

    // Cleanup Exchange mappings.
    for (size_t i = 0; i < request->GetExchangeCount(); ++i) {
        R_TRY(client_page_table->CleanupForIpcClient(request->GetExchangeClientAddress(i),
                                                     request->GetExchangeSize(i),
                                                     request->GetExchangeMemoryState(i)));
    }

    R_SUCCEED();
}

Result CleanupMap(KSessionRequest* request, KProcess* server_process,
                  KProcessPageTable* client_page_table) {
    // Cleanup the server map.
    R_TRY(CleanupServerMap(request, server_process));

    // Cleanup the client map.
    R_TRY(CleanupClientMap(request, client_page_table));

    R_SUCCEED();
}

Result ProcessReceiveMessageMapAliasDescriptors(int& offset, KProcessPageTable& dst_page_table,
                                                KProcessPageTable& src_page_table,
                                                const MessageBuffer& dst_msg,
                                                const MessageBuffer& src_msg,
                                                KSessionRequest* request, KMemoryPermission perm,
                                                bool send) {
    // Get the offset at the start of processing.
    const int cur_offset = offset;

    // Get the map alias descriptor.
    MessageBuffer::MapAliasDescriptor src_desc(src_msg, cur_offset);
    offset += static_cast<int>(MessageBuffer::MapAliasDescriptor::GetDataSize() / sizeof(u32));

    // Extract address/size.
    const KProcessAddress src_address = src_desc.GetAddress();
    const size_t size = src_desc.GetSize();
    KProcessAddress dst_address = 0;

    // Determine the result memory state.
    KMemoryState dst_state;
    R_TRY(GetMapAliasMemoryState(dst_state, src_desc.GetAttribute()));

    // Process the buffer, if it has a size.
    if (size > 0) {
        // Set up the source pages for ipc.
        R_TRY(dst_page_table.SetupForIpc(std::addressof(dst_address), size, src_address,
                                         src_page_table, perm, dst_state, send));

        // Ensure that we clean up on failure.
        ON_RESULT_FAILURE {
            dst_page_table.CleanupForIpcServer(dst_address, size, dst_state);
            src_page_table.CleanupForIpcClient(src_address, size, dst_state);
        };

        // Push the appropriate mapping.
        if (perm == KMemoryPermission::UserRead) {
            R_TRY(request->PushSend(src_address, dst_address, size, dst_state));
        } else if (send) {
            R_TRY(request->PushExchange(src_address, dst_address, size, dst_state));
        } else {
            R_TRY(request->PushReceive(src_address, dst_address, size, dst_state));
        }
    }

    // Set the output descriptor.
    dst_msg.Set(cur_offset,
                MessageBuffer::MapAliasDescriptor(reinterpret_cast<void*>(GetInteger(dst_address)),
                                                  size, src_desc.GetAttribute()));

    R_SUCCEED();
}

Result ReceiveMessage(KernelCore& kernel, bool& recv_list_broken, uint64_t dst_message_buffer,
                      size_t dst_buffer_size, KPhysicalAddress dst_message_paddr,
                      KThread& src_thread, uint64_t src_message_buffer, size_t src_buffer_size,
                      KServerSession* session, KSessionRequest* request) {
    // Prepare variables for receive.
    KThread& dst_thread = GetCurrentThread(kernel);
    KProcess& dst_process = *(dst_thread.GetOwnerProcess());
    KProcess& src_process = *(src_thread.GetOwnerProcess());
    auto& dst_page_table = dst_process.GetPageTable();
    auto& src_page_table = src_process.GetPageTable();

    // NOTE: Session is used only for debugging, and so may go unused.
    (void)session;

    // The receive list is initially not broken.
    recv_list_broken = false;

    // Set the server process for the request.
    request->SetServerProcess(std::addressof(dst_process));

    // Determine the message buffers.
    u32 *dst_msg_ptr, *src_msg_ptr;
    bool dst_user, src_user;

    if (dst_message_buffer) {
        dst_msg_ptr = kernel.System().DeviceMemory().GetPointer<u32>(dst_message_paddr);
        dst_user = true;
    } else {
        dst_msg_ptr = dst_page_table.GetMemory().GetPointer<u32>(dst_thread.GetTlsAddress());
        dst_buffer_size = MessageBufferSize;
        dst_message_buffer = GetInteger(dst_thread.GetTlsAddress());
        dst_user = false;
    }

    if (src_message_buffer) {
        // NOTE: Nintendo does not check the result of this GetPhysicalAddress call.
        src_msg_ptr = src_page_table.GetMemory().GetPointer<u32>(src_message_buffer);
        src_user = true;
    } else {
        src_msg_ptr = src_page_table.GetMemory().GetPointer<u32>(src_thread.GetTlsAddress());
        src_buffer_size = MessageBufferSize;
        src_message_buffer = GetInteger(src_thread.GetTlsAddress());
        src_user = false;
    }

    // Parse the headers.
    const MessageBuffer dst_msg(dst_msg_ptr, dst_buffer_size);
    const MessageBuffer src_msg(src_msg_ptr, src_buffer_size);
    const MessageBuffer::MessageHeader dst_header(dst_msg);
    const MessageBuffer::MessageHeader src_header(src_msg);
    const MessageBuffer::SpecialHeader dst_special_header(dst_msg, dst_header);
    const MessageBuffer::SpecialHeader src_special_header(src_msg, src_header);

    // Get the end of the source message.
    const size_t src_end_offset =
        MessageBuffer::GetRawDataIndex(src_header, src_special_header) + src_header.GetRawCount();

    // Ensure that the headers fit.
    R_UNLESS(MessageBuffer::GetMessageBufferSize(dst_header, dst_special_header) <= dst_buffer_size,
             ResultInvalidCombination);
    R_UNLESS(MessageBuffer::GetMessageBufferSize(src_header, src_special_header) <= src_buffer_size,
             ResultInvalidCombination);

    // Ensure the receive list offset is after the end of raw data.
    if (dst_header.GetReceiveListOffset()) {
        R_UNLESS(dst_header.GetReceiveListOffset() >=
                     MessageBuffer::GetRawDataIndex(dst_header, dst_special_header) +
                         dst_header.GetRawCount(),
                 ResultInvalidCombination);
    }

    // Ensure that the destination buffer is big enough to receive the source.
    R_UNLESS(dst_buffer_size >= src_end_offset * sizeof(u32), ResultMessageTooLarge);

    // Get the receive list.
    const s32 dst_recv_list_idx =
        MessageBuffer::GetReceiveListIndex(dst_header, dst_special_header);
    ReceiveList dst_recv_list(dst_msg_ptr, dst_message_buffer, dst_page_table, dst_header,
                              dst_special_header, dst_buffer_size, src_end_offset,
                              dst_recv_list_idx, !dst_user);

    // Ensure that the source special header isn't invalid.
    const bool src_has_special_header = src_header.GetHasSpecialHeader();
    if (src_has_special_header) {
        // Sending move handles from client -> server is not allowed.
        R_UNLESS(src_special_header.GetMoveHandleCount() == 0, ResultInvalidCombination);
    }

    // Prepare for further processing.
    int pointer_key = 0;
    int offset = dst_msg.Set(src_header);

    // Set up a guard to make sure that we end up in a clean state on error.
    ON_RESULT_FAILURE {
        // Cleanup mappings.
        CleanupMap(request, std::addressof(dst_process), std::addressof(src_page_table));

        // Cleanup special data.
        if (src_header.GetHasSpecialHeader()) {
            CleanupSpecialData(dst_process, dst_msg_ptr, dst_buffer_size);
        }

        // Cleanup the header if the receive list isn't broken.
        if (!recv_list_broken) {
            dst_msg.Set(dst_header);
            if (dst_header.GetHasSpecialHeader()) {
                dst_msg.Set(dst_special_header);
            }
        }
    };

    // Process any special data.
    if (src_header.GetHasSpecialHeader()) {
        // After we process, make sure we track whether the receive list is broken.
        SCOPE_EXIT {
            if (offset > dst_recv_list_idx) {
                recv_list_broken = true;
            }
        };

        // Process special data.
        R_TRY(ProcessMessageSpecialData<false>(offset, dst_process, src_process, src_thread,
                                               dst_msg, src_msg, src_special_header));
    }

    // Process any pointer buffers.
    for (auto i = 0; i < src_header.GetPointerCount(); ++i) {
        // After we process, make sure we track whether the receive list is broken.
        SCOPE_EXIT {
            if (offset > dst_recv_list_idx) {
                recv_list_broken = true;
            }
        };

        R_TRY(ProcessReceiveMessagePointerDescriptors(
            offset, pointer_key, dst_page_table, src_page_table, dst_msg, src_msg, dst_recv_list,
            dst_user && dst_header.GetReceiveListCount() ==
                            MessageBuffer::MessageHeader::ReceiveListCountType_ToMessageBuffer));
    }

    // Process any map alias buffers.
    for (auto i = 0; i < src_header.GetMapAliasCount(); ++i) {
        // After we process, make sure we track whether the receive list is broken.
        SCOPE_EXIT {
            if (offset > dst_recv_list_idx) {
                recv_list_broken = true;
            }
        };

        // We process in order send, recv, exch. Buffers after send (recv/exch) are ReadWrite.
        const KMemoryPermission perm = (i >= src_header.GetSendCount())
                                           ? KMemoryPermission::UserReadWrite
                                           : KMemoryPermission::UserRead;

        // Buffer is send if it is send or exch.
        const bool send = (i < src_header.GetSendCount()) ||
                          (i >= src_header.GetSendCount() + src_header.GetReceiveCount());

        R_TRY(ProcessReceiveMessageMapAliasDescriptors(offset, dst_page_table, src_page_table,
                                                       dst_msg, src_msg, request, perm, send));
    }

    // Process any raw data.
    if (const auto raw_count = src_header.GetRawCount(); raw_count != 0) {
        // After we process, make sure we track whether the receive list is broken.
        SCOPE_EXIT {
            if (offset + raw_count > dst_recv_list_idx) {
                recv_list_broken = true;
            }
        };

        // Get the offset and size.
        const size_t offset_words = offset * sizeof(u32);
        const size_t raw_size = raw_count * sizeof(u32);

        if (!dst_user && !src_user) {
            // Fast case is TLS -> TLS, do raw memcpy if we can.
            std::memcpy(dst_msg_ptr + offset, src_msg_ptr + offset, raw_size);
        } else if (dst_user) {
            // Determine how much fast size we can copy.
            const size_t max_fast_size = std::min<size_t>(offset_words + raw_size, PageSize);
            const size_t fast_size = max_fast_size - offset_words;

            // Determine source state; if user buffer, we require heap, and otherwise only linear
            // mapped (to enable tls use).
            const auto src_state =
                src_user ? KMemoryState::FlagReferenceCounted : KMemoryState::FlagLinearMapped;

            // Determine the source permission. User buffer should be unmapped + read, TLS should be
            // user readable.
            const KMemoryPermission src_perm = static_cast<KMemoryPermission>(
                src_user ? KMemoryPermission::NotMapped | KMemoryPermission::KernelRead
                         : KMemoryPermission::UserRead);

            // Perform the fast part of the copy.
            R_TRY(src_page_table.CopyMemoryFromLinearToKernel(
                dst_msg_ptr + offset, fast_size, src_message_buffer + offset_words, src_state,
                src_state, src_perm, KMemoryAttribute::Uncached, KMemoryAttribute::None));

            // If the fast part of the copy didn't get everything, perform the slow part of the
            // copy.
            if (fast_size < raw_size) {
                R_TRY(src_page_table.CopyMemoryFromHeapToHeap(
                    dst_page_table, dst_message_buffer + max_fast_size, raw_size - fast_size,
                    KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
                    KMemoryPermission::NotMapped | KMemoryPermission::KernelReadWrite,
                    KMemoryAttribute::Uncached | KMemoryAttribute::Locked, KMemoryAttribute::Locked,
                    src_message_buffer + max_fast_size, src_state, src_state, src_perm,
                    KMemoryAttribute::Uncached, KMemoryAttribute::None));
            }
        } else /* if (src_user) */ {
            // The source is a user buffer, so it should be unmapped + readable.
            constexpr KMemoryPermission SourcePermission = static_cast<KMemoryPermission>(
                KMemoryPermission::NotMapped | KMemoryPermission::KernelRead);

            // Copy the memory.
            R_TRY(src_page_table.CopyMemoryFromLinearToUser(
                dst_message_buffer + offset_words, raw_size, src_message_buffer + offset_words,
                KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
                SourcePermission, KMemoryAttribute::Uncached, KMemoryAttribute::None));
        }
    }

    // We succeeded!
    R_SUCCEED();
}

Result ProcessSendMessageReceiveMapping(KProcessPageTable& src_page_table,
                                        KProcessPageTable& dst_page_table,
                                        KProcessAddress client_address,
                                        KProcessAddress server_address, size_t size,
                                        KMemoryState src_state) {
    // If the size is zero, there's nothing to process.
    R_SUCCEED_IF(size == 0);

    // Get the memory state and attribute mask to test.
    KMemoryState test_state;
    KMemoryAttribute test_attr_mask;
    R_TRY(GetMapAliasTestStateAndAttributeMask(test_state, test_attr_mask, src_state));

    // Determine buffer extents.
    KProcessAddress aligned_dst_start = Common::AlignDown(GetInteger(client_address), PageSize);
    KProcessAddress aligned_dst_end = Common::AlignUp(GetInteger(client_address) + size, PageSize);
    KProcessAddress mapping_dst_start = Common::AlignUp(GetInteger(client_address), PageSize);
    KProcessAddress mapping_dst_end =
        Common::AlignDown(GetInteger(client_address) + size, PageSize);

    KProcessAddress mapping_src_end =
        Common::AlignDown(GetInteger(server_address) + size, PageSize);

    // If the start of the buffer is unaligned, handle that.
    if (aligned_dst_start != mapping_dst_start) {
        ASSERT(client_address < mapping_dst_start);
        const size_t copy_size = std::min<size_t>(size, mapping_dst_start - client_address);
        R_TRY(dst_page_table.CopyMemoryFromUserToLinear(
            client_address, copy_size, test_state, test_state, KMemoryPermission::UserReadWrite,
            test_attr_mask, KMemoryAttribute::None, server_address));
    }

    // If the end of the buffer is unaligned, handle that.
    if (mapping_dst_end < aligned_dst_end &&
        (aligned_dst_start == mapping_dst_start || aligned_dst_start < mapping_dst_end)) {
        const size_t copy_size = client_address + size - mapping_dst_end;
        R_TRY(dst_page_table.CopyMemoryFromUserToLinear(
            mapping_dst_end, copy_size, test_state, test_state, KMemoryPermission::UserReadWrite,
            test_attr_mask, KMemoryAttribute::None, mapping_src_end));
    }

    R_SUCCEED();
}

Result ProcessSendMessagePointerDescriptors(int& offset, int& pointer_key,
                                            KProcessPageTable& src_page_table,
                                            KProcessPageTable& dst_page_table,
                                            const MessageBuffer& dst_msg,
                                            const MessageBuffer& src_msg,
                                            const ReceiveList& dst_recv_list, bool dst_user) {
    // Get the offset at the start of processing.
    const int cur_offset = offset;

    // Get the pointer desc.
    MessageBuffer::PointerDescriptor src_desc(src_msg, cur_offset);
    offset += static_cast<int>(MessageBuffer::PointerDescriptor::GetDataSize() / sizeof(u32));

    // Extract address/size.
    const uint64_t src_pointer = src_desc.GetAddress();
    const size_t recv_size = src_desc.GetSize();
    uint64_t recv_pointer = 0;

    // Process the buffer, if it has a size.
    if (recv_size > 0) {
        // If using indexing, set index.
        if (dst_recv_list.IsIndex()) {
            pointer_key = src_desc.GetIndex();
        }

        // Get the buffer.
        dst_recv_list.GetBuffer(recv_pointer, recv_size, pointer_key);
        R_UNLESS(recv_pointer != 0, ResultOutOfResource);

        // Perform the pointer data copy.
        const bool dst_heap = dst_user && dst_recv_list.IsToMessageBuffer();
        const auto dst_state =
            dst_heap ? KMemoryState::FlagReferenceCounted : KMemoryState::FlagLinearMapped;
        const KMemoryPermission dst_perm =
            dst_heap ? KMemoryPermission::NotMapped | KMemoryPermission::KernelReadWrite
                     : KMemoryPermission::UserReadWrite;
        R_TRY(dst_page_table.CopyMemoryFromUserToLinear(
            recv_pointer, recv_size, dst_state, dst_state, dst_perm, KMemoryAttribute::Uncached,
            KMemoryAttribute::None, src_pointer));
    }

    // Set the output descriptor.
    dst_msg.Set(cur_offset, MessageBuffer::PointerDescriptor(reinterpret_cast<void*>(recv_pointer),
                                                             recv_size, src_desc.GetIndex()));

    R_SUCCEED();
}

Result SendMessage(KernelCore& kernel, uint64_t src_message_buffer, size_t src_buffer_size,
                   KPhysicalAddress src_message_paddr, KThread& dst_thread,
                   uint64_t dst_message_buffer, size_t dst_buffer_size, KServerSession* session,
                   KSessionRequest* request) {
    // Prepare variables for send.
    KThread& src_thread = GetCurrentThread(kernel);
    KProcess& dst_process = *(dst_thread.GetOwnerProcess());
    KProcess& src_process = *(src_thread.GetOwnerProcess());
    auto& dst_page_table = dst_process.GetPageTable();
    auto& src_page_table = src_process.GetPageTable();

    // NOTE: Session is used only for debugging, and so may go unused.
    (void)session;

    // Determine the message buffers.
    u32 *dst_msg_ptr, *src_msg_ptr;
    bool dst_user, src_user;

    if (dst_message_buffer) {
        // NOTE: Nintendo does not check the result of this GetPhysicalAddress call.
        dst_msg_ptr = dst_page_table.GetMemory().GetPointer<u32>(dst_message_buffer);
        dst_user = true;
    } else {
        dst_msg_ptr = dst_page_table.GetMemory().GetPointer<u32>(dst_thread.GetTlsAddress());
        dst_buffer_size = MessageBufferSize;
        dst_message_buffer = GetInteger(dst_thread.GetTlsAddress());
        dst_user = false;
    }

    if (src_message_buffer) {
        src_msg_ptr = src_page_table.GetMemory().GetPointer<u32>(src_message_buffer);
        src_user = true;
    } else {
        src_msg_ptr = src_page_table.GetMemory().GetPointer<u32>(src_thread.GetTlsAddress());
        src_buffer_size = MessageBufferSize;
        src_message_buffer = GetInteger(src_thread.GetTlsAddress());
        src_user = false;
    }

    // Parse the headers.
    const MessageBuffer dst_msg(dst_msg_ptr, dst_buffer_size);
    const MessageBuffer src_msg(src_msg_ptr, src_buffer_size);
    const MessageBuffer::MessageHeader dst_header(dst_msg);
    const MessageBuffer::MessageHeader src_header(src_msg);
    const MessageBuffer::SpecialHeader dst_special_header(dst_msg, dst_header);
    const MessageBuffer::SpecialHeader src_special_header(src_msg, src_header);

    // Get the end of the source message.
    const size_t src_end_offset =
        MessageBuffer::GetRawDataIndex(src_header, src_special_header) + src_header.GetRawCount();

    // Declare variables for processing.
    int offset = 0;
    int pointer_key = 0;
    bool processed_special_data = false;

    // Send the message.
    {
        // Make sure that we end up in a clean state on error.
        ON_RESULT_FAILURE {
            // Cleanup special data.
            if (processed_special_data) {
                if (src_header.GetHasSpecialHeader()) {
                    CleanupSpecialData(dst_process, dst_msg_ptr, dst_buffer_size);
                }
            } else {
                CleanupServerHandles(kernel, src_user ? src_message_buffer : 0, src_buffer_size,
                                     src_message_paddr);
            }

            // Cleanup mappings.
            CleanupMap(request, std::addressof(src_process), std::addressof(dst_page_table));
        };

        // Ensure that the headers fit.
        R_UNLESS(MessageBuffer::GetMessageBufferSize(src_header, src_special_header) <=
                     src_buffer_size,
                 ResultInvalidCombination);
        R_UNLESS(MessageBuffer::GetMessageBufferSize(dst_header, dst_special_header) <=
                     dst_buffer_size,
                 ResultInvalidCombination);

        // Ensure the receive list offset is after the end of raw data.
        if (dst_header.GetReceiveListOffset()) {
            R_UNLESS(dst_header.GetReceiveListOffset() >=
                         MessageBuffer::GetRawDataIndex(dst_header, dst_special_header) +
                             dst_header.GetRawCount(),
                     ResultInvalidCombination);
        }

        // Ensure that the destination buffer is big enough to receive the source.
        R_UNLESS(dst_buffer_size >= src_end_offset * sizeof(u32), ResultMessageTooLarge);

        // Replies must have no buffers.
        R_UNLESS(src_header.GetSendCount() == 0, ResultInvalidCombination);
        R_UNLESS(src_header.GetReceiveCount() == 0, ResultInvalidCombination);
        R_UNLESS(src_header.GetExchangeCount() == 0, ResultInvalidCombination);

        // Get the receive list.
        const s32 dst_recv_list_idx =
            MessageBuffer::GetReceiveListIndex(dst_header, dst_special_header);
        ReceiveList dst_recv_list(dst_msg_ptr, dst_message_buffer, dst_page_table, dst_header,
                                  dst_special_header, dst_buffer_size, src_end_offset,
                                  dst_recv_list_idx, !dst_user);

        // Handle any receive buffers.
        for (size_t i = 0; i < request->GetReceiveCount(); ++i) {
            R_TRY(ProcessSendMessageReceiveMapping(
                src_page_table, dst_page_table, request->GetReceiveClientAddress(i),
                request->GetReceiveServerAddress(i), request->GetReceiveSize(i),
                request->GetReceiveMemoryState(i)));
        }

        // Handle any exchange buffers.
        for (size_t i = 0; i < request->GetExchangeCount(); ++i) {
            R_TRY(ProcessSendMessageReceiveMapping(
                src_page_table, dst_page_table, request->GetExchangeClientAddress(i),
                request->GetExchangeServerAddress(i), request->GetExchangeSize(i),
                request->GetExchangeMemoryState(i)));
        }

        // Set the header.
        offset = dst_msg.Set(src_header);

        // Process any special data.
        ASSERT(GetCurrentThreadPointer(kernel) == std::addressof(src_thread));
        processed_special_data = true;
        if (src_header.GetHasSpecialHeader()) {
            R_TRY(ProcessMessageSpecialData<true>(offset, dst_process, src_process, src_thread,
                                                  dst_msg, src_msg, src_special_header));
        }

        // Process any pointer buffers.
        for (auto i = 0; i < src_header.GetPointerCount(); ++i) {
            R_TRY(ProcessSendMessagePointerDescriptors(
                offset, pointer_key, src_page_table, dst_page_table, dst_msg, src_msg,
                dst_recv_list,
                dst_user &&
                    dst_header.GetReceiveListCount() ==
                        MessageBuffer::MessageHeader::ReceiveListCountType_ToMessageBuffer));
        }

        // Clear any map alias buffers.
        for (auto i = 0; i < src_header.GetMapAliasCount(); ++i) {
            offset = dst_msg.Set(offset, MessageBuffer::MapAliasDescriptor());
        }

        // Process any raw data.
        if (const auto raw_count = src_header.GetRawCount(); raw_count != 0) {
            // Get the offset and size.
            const size_t offset_words = offset * sizeof(u32);
            const size_t raw_size = raw_count * sizeof(u32);

            if (!dst_user && !src_user) {
                // Fast case is TLS -> TLS, do raw memcpy if we can.
                std::memcpy(dst_msg_ptr + offset, src_msg_ptr + offset, raw_size);
            } else if (src_user) {
                // Determine how much fast size we can copy.
                const size_t max_fast_size = std::min<size_t>(offset_words + raw_size, PageSize);
                const size_t fast_size = max_fast_size - offset_words;

                // Determine dst state; if user buffer, we require heap, and otherwise only linear
                // mapped (to enable tls use).
                const auto dst_state =
                    dst_user ? KMemoryState::FlagReferenceCounted : KMemoryState::FlagLinearMapped;

                // Determine the dst permission. User buffer should be unmapped + read, TLS should
                // be user readable.
                const KMemoryPermission dst_perm =
                    dst_user ? KMemoryPermission::NotMapped | KMemoryPermission::KernelReadWrite
                             : KMemoryPermission::UserReadWrite;

                // Perform the fast part of the copy.
                R_TRY(dst_page_table.CopyMemoryFromKernelToLinear(
                    dst_message_buffer + offset_words, fast_size, dst_state, dst_state, dst_perm,
                    KMemoryAttribute::Uncached, KMemoryAttribute::None, src_msg_ptr + offset));

                // If the fast part of the copy didn't get everything, perform the slow part of the
                // copy.
                if (fast_size < raw_size) {
                    R_TRY(dst_page_table.CopyMemoryFromHeapToHeap(
                        dst_page_table, dst_message_buffer + max_fast_size, raw_size - fast_size,
                        dst_state, dst_state, dst_perm, KMemoryAttribute::Uncached,
                        KMemoryAttribute::None, src_message_buffer + max_fast_size,
                        KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
                        KMemoryPermission::NotMapped | KMemoryPermission::KernelRead,
                        KMemoryAttribute::Uncached | KMemoryAttribute::Locked,
                        KMemoryAttribute::Locked));
                }
            } else /* if (dst_user) */ {
                // The destination is a user buffer, so it should be unmapped + readable.
                constexpr KMemoryPermission DestinationPermission =
                    KMemoryPermission::NotMapped | KMemoryPermission::KernelReadWrite;

                // Copy the memory.
                R_TRY(dst_page_table.CopyMemoryFromUserToLinear(
                    dst_message_buffer + offset_words, raw_size, KMemoryState::FlagReferenceCounted,
                    KMemoryState::FlagReferenceCounted, DestinationPermission,
                    KMemoryAttribute::Uncached, KMemoryAttribute::None,
                    src_message_buffer + offset_words));
            }
        }
    }

    // Perform (and validate) any remaining cleanup.
    R_RETURN(CleanupMap(request, std::addressof(src_process), std::addressof(dst_page_table)));
}

void ReplyAsyncError(KProcess* to_process, uint64_t to_msg_buf, size_t to_msg_buf_size,
                     Result result) {
    // Convert the address to a linear pointer.
    u32* to_msg = to_process->GetMemory().GetPointer<u32>(to_msg_buf);

    // Set the error.
    MessageBuffer msg(to_msg, to_msg_buf_size);
    msg.SetAsyncResult(result);
}

} // namespace

KServerSession::KServerSession(KernelCore& kernel)
    : KSynchronizationObject{kernel}, m_lock{m_kernel} {}

KServerSession::~KServerSession() = default;

void KServerSession::Destroy() {
    m_parent->OnServerClosed();

    this->CleanupRequests();

    m_parent->Close();
}

Result KServerSession::ReceiveRequest(uintptr_t server_message, uintptr_t server_buffer_size,
                                      KPhysicalAddress server_message_paddr,
                                      std::shared_ptr<Service::HLERequestContext>* out_context,
                                      std::weak_ptr<Service::SessionRequestManager> manager) {
    // Lock the session.
    KScopedLightLock lk{m_lock};

    // Get the request and client thread.
    KSessionRequest* request;
    KThread* client_thread;

    {
        KScopedSchedulerLock sl{m_kernel};

        // Ensure that we can service the request.
        R_UNLESS(!m_parent->IsClientClosed(), ResultSessionClosed);

        // Ensure we aren't already servicing a request.
        R_UNLESS(m_current_request == nullptr, ResultNotFound);

        // Ensure we have a request to service.
        R_UNLESS(!m_request_list.empty(), ResultNotFound);

        // Pop the first request from the list.
        request = std::addressof(m_request_list.front());
        m_request_list.pop_front();

        // Get the thread for the request.
        client_thread = request->GetThread();
        R_UNLESS(client_thread != nullptr, ResultSessionClosed);

        // Open the client thread.
        client_thread->Open();
    }

    SCOPE_EXIT {
        client_thread->Close();
    };

    // Set the request as our current.
    m_current_request = request;

    // Get the client address.
    uint64_t client_message = request->GetAddress();
    size_t client_buffer_size = request->GetSize();
    bool recv_list_broken = false;

    // Receive the message.
    Result result = ResultSuccess;

    if (out_context != nullptr) {
        // HLE request.
        if (!client_message) {
            client_message = GetInteger(client_thread->GetTlsAddress());
        }
        Core::Memory::Memory& memory{client_thread->GetOwnerProcess()->GetMemory()};
        u32* cmd_buf{reinterpret_cast<u32*>(memory.GetPointer(client_message))};
        *out_context =
            std::make_shared<Service::HLERequestContext>(m_kernel, memory, this, client_thread);
        (*out_context)->SetSessionRequestManager(manager);
        (*out_context)->PopulateFromIncomingCommandBuffer(cmd_buf);
        // We succeeded.
        R_SUCCEED();
    } else {
        result = ReceiveMessage(m_kernel, recv_list_broken, server_message, server_buffer_size,
                                server_message_paddr, *client_thread, client_message,
                                client_buffer_size, this, request);
    }

    // Handle cleanup on receive failure.
    if (R_FAILED(result)) {
        // Cache the result to return it to the client.
        const Result result_for_client = result;

        // Clear the current request.
        {
            KScopedSchedulerLock sl(m_kernel);
            ASSERT(m_current_request == request);
            m_current_request = nullptr;
            if (!m_request_list.empty()) {
                this->NotifyAvailable();
            }
        }

        // Reply to the client.
        {
            // After we reply, close our reference to the request.
            SCOPE_EXIT {
                request->Close();
            };

            // Get the event to check whether the request is async.
            if (KEvent* event = request->GetEvent(); event != nullptr) {
                // The client sent an async request.
                KProcess* client = client_thread->GetOwnerProcess();
                auto& client_pt = client->GetPageTable();

                // Send the async result.
                if (R_FAILED(result_for_client)) {
                    ReplyAsyncError(client, client_message, client_buffer_size, result_for_client);
                }

                // Unlock the client buffer.
                // NOTE: Nintendo does not check the result of this.
                client_pt.UnlockForIpcUserBuffer(client_message, client_buffer_size);

                // Signal the event.
                event->Signal();
            } else {
                // End the client thread's wait.
                KScopedSchedulerLock sl(m_kernel);

                if (!client_thread->IsTerminationRequested()) {
                    client_thread->EndWait(result_for_client);
                }
            }
        }

        // Set the server result.
        if (recv_list_broken) {
            result = ResultReceiveListBroken;
        } else {
            result = ResultNotFound;
        }
    }

    R_RETURN(result);
}

Result KServerSession::SendReply(uintptr_t server_message, uintptr_t server_buffer_size,
                                 KPhysicalAddress server_message_paddr, bool is_hle) {
    // Lock the session.
    KScopedLightLock lk{m_lock};

    // Get the request.
    KSessionRequest* request;
    {
        KScopedSchedulerLock sl{m_kernel};

        // Get the current request.
        request = m_current_request;
        R_UNLESS(request != nullptr, ResultInvalidState);

        // Clear the current request, since we're processing it.
        m_current_request = nullptr;
        if (!m_request_list.empty()) {
            this->NotifyAvailable();
        }
    }

    // Close reference to the request once we're done processing it.
    SCOPE_EXIT {
        request->Close();
    };

    // Extract relevant information from the request.
    const uint64_t client_message = request->GetAddress();
    const size_t client_buffer_size = request->GetSize();
    KThread* client_thread = request->GetThread();
    KEvent* event = request->GetEvent();

    // Check whether we're closed.
    const bool closed = (client_thread == nullptr || m_parent->IsClientClosed());

    Result result = ResultSuccess;
    if (!closed) {
        // If we're not closed, send the reply.
        if (is_hle) {
            // HLE servers write directly to a pointer to the thread command buffer. Therefore
            // the reply has already been written in this case.
        } else {
            result = SendMessage(m_kernel, server_message, server_buffer_size, server_message_paddr,
                                 *client_thread, client_message, client_buffer_size, this, request);
        }
    } else if (!is_hle) {
        // Otherwise, we'll need to do some cleanup.
        KProcess* server_process = request->GetServerProcess();
        KProcess* client_process =
            (client_thread != nullptr) ? client_thread->GetOwnerProcess() : nullptr;
        KProcessPageTable* client_page_table =
            (client_process != nullptr) ? std::addressof(client_process->GetPageTable()) : nullptr;

        // Cleanup server handles.
        result = CleanupServerHandles(m_kernel, server_message, server_buffer_size,
                                      server_message_paddr);

        // Cleanup mappings.
        Result cleanup_map_result = CleanupMap(request, server_process, client_page_table);

        // If we successfully cleaned up handles, use the map cleanup result as our result.
        if (R_SUCCEEDED(result)) {
            result = cleanup_map_result;
        }
    }

    // Select a result for the client.
    Result client_result = result;
    if (closed && R_SUCCEEDED(result)) {
        result = ResultSessionClosed;
        client_result = ResultSessionClosed;
    } else {
        result = ResultSuccess;
    }

    // If there's a client thread, update it.
    if (client_thread != nullptr) {
        if (event != nullptr) {
            // Get the client process/page table.
            KProcess* client_process = client_thread->GetOwnerProcess();
            KProcessPageTable* client_page_table = std::addressof(client_process->GetPageTable());

            // If we need to, reply with an async error.
            if (R_FAILED(client_result)) {
                ReplyAsyncError(client_process, client_message, client_buffer_size, client_result);
            }

            // Unlock the client buffer.
            // NOTE: Nintendo does not check the result of this.
            client_page_table->UnlockForIpcUserBuffer(client_message, client_buffer_size);

            // Signal the event.
            event->Signal();
        } else {
            // End the client thread's wait.
            KScopedSchedulerLock sl{m_kernel};

            if (!client_thread->IsTerminationRequested()) {
                client_thread->EndWait(client_result);
            }
        }
    }

    R_RETURN(result);
}

Result KServerSession::OnRequest(KSessionRequest* request) {
    // Create the wait queue.
    ThreadQueueImplForKServerSessionRequest wait_queue{m_kernel};

    {
        // Lock the scheduler.
        KScopedSchedulerLock sl{m_kernel};

        // Ensure that we can handle new requests.
        R_UNLESS(!m_parent->IsServerClosed(), ResultSessionClosed);

        // Check that we're not terminating.
        R_UNLESS(!GetCurrentThread(m_kernel).IsTerminationRequested(), ResultTerminationRequested);

        // Get whether we're empty.
        const bool was_empty = m_request_list.empty();

        // Add the request to the list.
        request->Open();
        m_request_list.push_back(*request);

        // If we were empty, signal.
        if (was_empty) {
            this->NotifyAvailable();
        }

        // If we have a request event, this is asynchronous, and we don't need to wait.
        R_SUCCEED_IF(request->GetEvent() != nullptr);

        // This is a synchronous request, so we should wait for our request to complete.
        GetCurrentThread(m_kernel).SetWaitReasonForDebugging(ThreadWaitReasonForDebugging::IPC);
        GetCurrentThread(m_kernel).BeginWait(std::addressof(wait_queue));
    }

    return GetCurrentThread(m_kernel).GetWaitResult();
}

bool KServerSession::IsSignaled() const {
    ASSERT(KScheduler::IsSchedulerLockedByCurrentThread(m_kernel));

    // If the client is closed, we're always signaled.
    if (m_parent->IsClientClosed()) {
        return true;
    }

    // Otherwise, we're signaled if we have a request and aren't handling one.
    return !m_request_list.empty() && m_current_request == nullptr;
}

void KServerSession::CleanupRequests() {
    KScopedLightLock lk(m_lock);

    // Clean up any pending requests.
    while (true) {
        // Get the next request.
        KSessionRequest* request = nullptr;
        {
            KScopedSchedulerLock sl{m_kernel};

            if (m_current_request) {
                // Choose the current request if we have one.
                request = m_current_request;
                m_current_request = nullptr;
            } else if (!m_request_list.empty()) {
                // Pop the request from the front of the list.
                request = std::addressof(m_request_list.front());
                m_request_list.pop_front();
            }
        }

        // If there's no request, we're done.
        if (request == nullptr) {
            break;
        }

        // Close a reference to the request once it's cleaned up.
        SCOPE_EXIT {
            request->Close();
        };

        // Extract relevant information from the request.
        const uint64_t client_message = request->GetAddress();
        const size_t client_buffer_size = request->GetSize();
        KThread* client_thread = request->GetThread();
        KEvent* event = request->GetEvent();

        KProcess* server_process = request->GetServerProcess();
        KProcess* client_process =
            (client_thread != nullptr) ? client_thread->GetOwnerProcess() : nullptr;
        KProcessPageTable* client_page_table =
            (client_process != nullptr) ? std::addressof(client_process->GetPageTable()) : nullptr;

        // Cleanup the mappings.
        Result result = CleanupMap(request, server_process, client_page_table);

        // If there's a client thread, update it.
        if (client_thread != nullptr) {
            if (event != nullptr) {
                // We need to reply async.
                ReplyAsyncError(client_process, client_message, client_buffer_size,
                                (R_SUCCEEDED(result) ? ResultSessionClosed : result));

                // Unlock the client buffer.
                // NOTE: Nintendo does not check the result of this.
                client_page_table->UnlockForIpcUserBuffer(client_message, client_buffer_size);

                // Signal the event.
                event->Signal();
            } else {
                // End the client thread's wait.
                KScopedSchedulerLock sl{m_kernel};

                if (!client_thread->IsTerminationRequested()) {
                    client_thread->EndWait(ResultSessionClosed);
                }
            }
        }
    }
}

void KServerSession::OnClientClosed() {
    KScopedLightLock lk{m_lock};

    // Handle any pending requests.
    KSessionRequest* prev_request = nullptr;
    while (true) {
        // Declare variables for processing the request.
        KSessionRequest* request = nullptr;
        KEvent* event = nullptr;
        KThread* thread = nullptr;
        bool cur_request = false;
        bool terminate = false;

        // Get the next request.
        {
            KScopedSchedulerLock sl{m_kernel};

            if (m_current_request != nullptr && m_current_request != prev_request) {
                // Set the request, open a reference as we process it.
                request = m_current_request;
                request->Open();
                cur_request = true;

                // Get thread and event for the request.
                thread = request->GetThread();
                event = request->GetEvent();

                // If the thread is terminating, handle that.
                if (thread->IsTerminationRequested()) {
                    request->ClearThread();
                    request->ClearEvent();
                    terminate = true;
                }

                prev_request = request;
            } else if (!m_request_list.empty()) {
                // Pop the request from the front of the list.
                request = std::addressof(m_request_list.front());
                m_request_list.pop_front();

                // Get thread and event for the request.
                thread = request->GetThread();
                event = request->GetEvent();
            }
        }

        // If there are no requests, we're done.
        if (request == nullptr) {
            break;
        }

        // All requests must have threads.
        ASSERT(thread != nullptr);

        // Ensure that we close the request when done.
        SCOPE_EXIT {
            request->Close();
        };

        // If we're terminating, close a reference to the thread and event.
        if (terminate) {
            thread->Close();
            if (event != nullptr) {
                event->Close();
            }
        }

        // If we need to, reply.
        if (event != nullptr && !cur_request) {
            // There must be no mappings.
            ASSERT(request->GetSendCount() == 0);
            ASSERT(request->GetReceiveCount() == 0);
            ASSERT(request->GetExchangeCount() == 0);

            // Get the process and page table.
            KProcess* client_process = thread->GetOwnerProcess();
            auto& client_pt = client_process->GetPageTable();

            // Reply to the request.
            ReplyAsyncError(client_process, request->GetAddress(), request->GetSize(),
                            ResultSessionClosed);

            // Unlock the buffer.
            // NOTE: Nintendo does not check the result of this.
            client_pt.UnlockForIpcUserBuffer(request->GetAddress(), request->GetSize());

            // Signal the event.
            event->Signal();
        }
    }

    // Notify.
    this->NotifyAvailable(ResultSessionClosed);
}

} // namespace Kernel
