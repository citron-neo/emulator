// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/alignment.h"
#include "common/bit_field.h"
#include "core/hle/kernel/k_thread.h"

namespace Kernel {

constexpr inline size_t MessageBufferSize = 0x100;

class MessageBuffer {
public:
    class MessageHeader {
    private:
        static constexpr inline u64 NullTag = 0;

    public:
        enum ReceiveListCountType : u32 {
            ReceiveListCountType_None = 0,
            ReceiveListCountType_ToMessageBuffer = 1,
            ReceiveListCountType_ToSingleBuffer = 2,

            ReceiveListCountType_CountOffset = 2,
            ReceiveListCountType_CountMax = 13,
        };

    private:
        union {
            std::array<u32, 2> raw;

            struct {
                // Define fields for the first header word.
                union {
                    BitField<0, 16, u16> tag;
                    BitField<16, 4, u32> pointer_count;
                    BitField<20, 4, u32> send_count;
                    BitField<24, 4, u32> receive_count;
                    BitField<28, 4, u32> exchange_count;
                };

                // Define fields for the second header word.
                union {
                    BitField<0, 10, u32> raw_count;
                    BitField<10, 4, ReceiveListCountType> receive_list_count;
                    BitField<14, 6, u32> reserved0;
                    BitField<20, 11, u32> receive_list_offset;
                    BitField<31, 1, u32> has_special_header;
                };
            };
        } m_header;

    public:
        constexpr MessageHeader() : m_header{} {}

        constexpr MessageHeader(u16 tag, bool special, s32 ptr, s32 send, s32 recv, s32 exch,
                                s32 raw, ReceiveListCountType recv_list)
            : m_header{} {
            m_header.raw[0] = 0;
            m_header.raw[1] = 0;

            m_header.tag.Assign(tag);
            m_header.pointer_count.Assign(ptr);
            m_header.send_count.Assign(send);
            m_header.receive_count.Assign(recv);
            m_header.exchange_count.Assign(exch);

            m_header.raw_count.Assign(raw);
            m_header.receive_list_count.Assign(recv_list);
            m_header.has_special_header.Assign(special);
        }

        explicit MessageHeader(const MessageBuffer& buf) : m_header{} {
            buf.Get(0, m_header.raw.data(), 2);
        }

        explicit MessageHeader(const u32* msg) : m_header{{msg[0], msg[1]}} {}

        constexpr u16 GetTag() const {
            return m_header.tag;
        }

        constexpr s32 GetPointerCount() const {
            return m_header.pointer_count;
        }

        constexpr s32 GetSendCount() const {
            return m_header.send_count;
        }

        constexpr s32 GetReceiveCount() const {
            return m_header.receive_count;
        }

        constexpr s32 GetExchangeCount() const {
            return m_header.exchange_count;
        }

        constexpr s32 GetMapAliasCount() const {
            return this->GetSendCount() + this->GetReceiveCount() + this->GetExchangeCount();
        }

        constexpr s32 GetRawCount() const {
            return m_header.raw_count;
        }

        constexpr ReceiveListCountType GetReceiveListCount() const {
            return m_header.receive_list_count;
        }

        constexpr s32 GetReceiveListOffset() const {
            return m_header.receive_list_offset;
        }

        constexpr bool GetHasSpecialHeader() const {
            return m_header.has_special_header.Value() != 0;
        }

        constexpr void SetReceiveListCount(ReceiveListCountType recv_list) {
            m_header.receive_list_count.Assign(recv_list);
        }

        constexpr const u32* GetData() const {
            return m_header.raw.data();
        }

        static constexpr size_t GetDataSize() {
            return sizeof(m_header);
        }
    };

    class SpecialHeader {
    private:
        union {
            std::array<u32, 1> raw;

            // Define fields for the header word.
            BitField<0, 1, u32> has_process_id;
            BitField<1, 4, u32> copy_handle_count;
            BitField<5, 4, u32> move_handle_count;
        } m_header;
        bool m_has_header;

    public:
        constexpr explicit SpecialHeader(bool pid, s32 copy, s32 move)
            : m_header{}, m_has_header(true) {
            m_header.has_process_id.Assign(pid);
            m_header.copy_handle_count.Assign(copy);
            m_header.move_handle_count.Assign(move);
        }

        constexpr explicit SpecialHeader(bool pid, s32 copy, s32 move, bool _has_header)
            : m_header{}, m_has_header(_has_header) {
            m_header.has_process_id.Assign(pid);
            m_header.copy_handle_count.Assign(copy);
            m_header.move_handle_count.Assign(move);
        }

        explicit SpecialHeader(const MessageBuffer& buf, const MessageHeader& hdr)
            : m_header{}, m_has_header(hdr.GetHasSpecialHeader()) {
            if (m_has_header) {
                buf.Get(static_cast<s32>(MessageHeader::GetDataSize() / sizeof(u32)),
                        m_header.raw.data(), sizeof(m_header) / sizeof(u32));
            }
        }

        constexpr bool GetHasProcessId() const {
            return m_header.has_process_id.Value() != 0;
        }

        constexpr s32 GetCopyHandleCount() const {
            return m_header.copy_handle_count;
        }

        constexpr s32 GetMoveHandleCount() const {
            return m_header.move_handle_count;
        }

        constexpr const u32* GetHeader() const {
            return m_header.raw.data();
        }

        constexpr size_t GetHeaderSize() const {
            if (m_has_header) {
                return sizeof(m_header);
            } else {
                return 0;
            }
        }

        constexpr size_t GetDataSize() const {
            if (m_has_header) {
                return (this->GetHasProcessId() ? sizeof(u64) : 0) +
                       (this->GetCopyHandleCount() * sizeof(Handle)) +
                       (this->GetMoveHandleCount() * sizeof(Handle));
            } else {
                return 0;
            }
        }
    };

    class MapAliasDescriptor {
    public:
        enum class Attribute : u32 {
            Ipc = 0,
            NonSecureIpc = 1,
            NonDeviceIpc = 3,
        };

    private:
        static constexpr u32 SizeLowCount = 32;
        static constexpr u32 SizeHighCount = 4;
        static constexpr u32 AddressLowCount = 32;
        static constexpr u32 AddressMidCount = 4;

        constexpr u32 GetAddressMid(u64 address) {
            return static_cast<u32>(address >> AddressLowCount) & ((1U << AddressMidCount) - 1);
        }

        constexpr u32 GetAddressHigh(u64 address) {
            return static_cast<u32>(address >> (AddressLowCount + AddressMidCount));
        }

    private:
        union {
            std::array<u32, 3> raw;

            struct {
                // Define fields for the first two words.
                u32 size_low;
                u32 address_low;

                // Define fields for the packed descriptor word.
                union {
                    BitField<0, 2, Attribute> attributes;
                    BitField<2, 3, u32> address_high;
                    BitField<5, 19, u32> reserved;
                    BitField<24, 4, u32> size_high;
                    BitField<28, 4, u32> address_mid;
                };
            };
        } m_data;

    public:
        constexpr MapAliasDescriptor() : m_data{} {}

        MapAliasDescriptor(const void* buffer, size_t _size, Attribute attr = Attribute::Ipc)
            : m_data{} {
            const u64 address = reinterpret_cast<u64>(buffer);
            const u64 size = static_cast<u64>(_size);
            m_data.size_low = static_cast<u32>(size);
            m_data.address_low = static_cast<u32>(address);
            m_data.attributes.Assign(attr);
            m_data.address_mid.Assign(GetAddressMid(address));
            m_data.size_high.Assign(static_cast<u32>(size >> SizeLowCount));
            m_data.address_high.Assign(GetAddressHigh(address));
        }

        MapAliasDescriptor(const MessageBuffer& buf, s32 index) : m_data{} {
            buf.Get(index, m_data.raw.data(), 3);
        }

        constexpr uintptr_t GetAddress() const {
            return (static_cast<u64>((m_data.address_high << AddressMidCount) | m_data.address_mid)
                    << AddressLowCount) |
                   m_data.address_low;
        }

        constexpr uintptr_t GetSize() const {
            return (static_cast<u64>(m_data.size_high) << SizeLowCount) | m_data.size_low;
        }

        constexpr Attribute GetAttribute() const {
            return m_data.attributes;
        }

        constexpr const u32* GetData() const {
            return m_data.raw.data();
        }

        static constexpr size_t GetDataSize() {
            return sizeof(m_data);
        }
    };

    class PointerDescriptor {
    private:
        static constexpr u32 AddressLowCount = 32;
        static constexpr u32 AddressMidCount = 4;

        constexpr u32 GetAddressMid(u64 address) {
            return static_cast<u32>(address >> AddressLowCount) & ((1u << AddressMidCount) - 1);
        }

        constexpr u32 GetAddressHigh(u64 address) {
            return static_cast<u32>(address >> (AddressLowCount + AddressMidCount));
        }

    private:
        union {
            std::array<u32, 2> raw;

            struct {
                // Define fields for the packed descriptor word.
                union {
                    BitField<0, 4, u32> index;
                    BitField<4, 2, u32> reserved0;
                    BitField<6, 3, u32> address_high;
                    BitField<9, 3, u32> reserved1;
                    BitField<12, 4, u32> address_mid;
                    BitField<16, 16, u32> size;
                };

                // Define fields for the second word.
                u32 address_low;
            };
        } m_data;

    public:
        constexpr PointerDescriptor() : m_data{} {}

        PointerDescriptor(const void* buffer, size_t size, s32 index) : m_data{} {
            const u64 address = reinterpret_cast<u64>(buffer);

            m_data.index.Assign(index);
            m_data.address_high.Assign(GetAddressHigh(address));
            m_data.address_mid.Assign(GetAddressMid(address));
            m_data.size.Assign(static_cast<u32>(size));

            m_data.address_low = static_cast<u32>(address);
        }

        PointerDescriptor(const MessageBuffer& buf, s32 index) : m_data{} {
            buf.Get(index, m_data.raw.data(), 2);
        }

        constexpr s32 GetIndex() const {
            return m_data.index;
        }

        constexpr uintptr_t GetAddress() const {
            return (static_cast<u64>((m_data.address_high << AddressMidCount) | m_data.address_mid)
                    << AddressLowCount) |
                   m_data.address_low;
        }

        constexpr size_t GetSize() const {
            return m_data.size;
        }

        constexpr const u32* GetData() const {
            return m_data.raw.data();
        }

        static constexpr size_t GetDataSize() {
            return sizeof(m_data);
        }
    };

    class ReceiveListEntry {
    private:
        static constexpr u32 AddressLowCount = 32;

        constexpr u32 GetAddressHigh(u64 address) {
            return static_cast<u32>(address >> (AddressLowCount));
        }

    private:
        union {
            std::array<u32, 2> raw;

            struct {
                // Define fields for the first word.
                u32 address_low;

                // Define fields for the packed descriptor word.
                union {
                    BitField<0, 7, u32> address_high;
                    BitField<7, 9, u32> reserved;
                    BitField<16, 16, u32> size;
                };
            };
        } m_data;

    public:
        constexpr ReceiveListEntry() : m_data{} {}

        ReceiveListEntry(const void* buffer, size_t size) : m_data{} {
            const u64 address = reinterpret_cast<u64>(buffer);

            m_data.address_low = static_cast<u32>(address);

            m_data.address_high.Assign(GetAddressHigh(address));
            m_data.size.Assign(static_cast<u32>(size));
        }

        ReceiveListEntry(u32 a, u32 b) : m_data{{a, b}} {}

        constexpr uintptr_t GetAddress() const {
            return (static_cast<u64>(m_data.address_high) << AddressLowCount) | m_data.address_low;
        }

        constexpr size_t GetSize() const {
            return m_data.size;
        }

        constexpr const u32* GetData() const {
            return m_data.raw.data();
        }

        static constexpr size_t GetDataSize() {
            return sizeof(m_data);
        }
    };

private:
    u32* m_buffer;
    size_t m_size;

public:
    constexpr MessageBuffer(u32* b, size_t sz) : m_buffer(b), m_size(sz) {}
    constexpr explicit MessageBuffer(u32* b) : m_buffer(b), m_size(MessageBufferSize) {}

    constexpr void* GetBufferForDebug() const {
        return m_buffer;
    }

    constexpr size_t GetBufferSize() const {
        return m_size;
    }

    void Get(s32 index, u32* dst, size_t count) const {
        // Ensure that this doesn't get re-ordered.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Get the words.
        static_assert(sizeof(*dst) == sizeof(*m_buffer));

        memcpy(dst, m_buffer + index, count * sizeof(*dst));
    }

    s32 Set(s32 index, u32* src, size_t count) const {
        // Ensure that this doesn't get re-ordered.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Set the words.
        memcpy(m_buffer + index, src, count * sizeof(*src));

        // Ensure that this doesn't get re-ordered.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        return static_cast<s32>(index + count);
    }

    template <typename T>
    const T& GetRaw(s32 index) const {
        return *reinterpret_cast<const T*>(m_buffer + index);
    }

    template <typename T>
    s32 SetRaw(s32 index, const T& val) const {
        *reinterpret_cast<const T*>(m_buffer + index) = val;
        return index + (Common::AlignUp(sizeof(val), sizeof(*m_buffer)) / sizeof(*m_buffer));
    }

    void GetRawArray(s32 index, void* dst, size_t len) const {
        memcpy(dst, m_buffer + index, len);
    }

    void SetRawArray(s32 index, const void* src, size_t len) const {
        memcpy(m_buffer + index, src, len);
    }

    void SetNull() const {
        this->Set(MessageHeader());
    }

    s32 Set(const MessageHeader& hdr) const {
        memcpy(m_buffer, hdr.GetData(), hdr.GetDataSize());
        return static_cast<s32>(hdr.GetDataSize() / sizeof(*m_buffer));
    }

    s32 Set(const SpecialHeader& spc) const {
        const s32 index = static_cast<s32>(MessageHeader::GetDataSize() / sizeof(*m_buffer));
        memcpy(m_buffer + index, spc.GetHeader(), spc.GetHeaderSize());
        return static_cast<s32>(index + (spc.GetHeaderSize() / sizeof(*m_buffer)));
    }

    s32 SetHandle(s32 index, const Handle& hnd) const {
        memcpy(m_buffer + index, std::addressof(hnd), sizeof(hnd));
        return static_cast<s32>(index + (sizeof(hnd) / sizeof(*m_buffer)));
    }

    s32 SetProcessId(s32 index, const u64 pid) const {
        memcpy(m_buffer + index, std::addressof(pid), sizeof(pid));
        return static_cast<s32>(index + (sizeof(pid) / sizeof(*m_buffer)));
    }

    s32 Set(s32 index, const MapAliasDescriptor& desc) const {
        memcpy(m_buffer + index, desc.GetData(), desc.GetDataSize());
        return static_cast<s32>(index + (desc.GetDataSize() / sizeof(*m_buffer)));
    }

    s32 Set(s32 index, const PointerDescriptor& desc) const {
        memcpy(m_buffer + index, desc.GetData(), desc.GetDataSize());
        return static_cast<s32>(index + (desc.GetDataSize() / sizeof(*m_buffer)));
    }

    s32 Set(s32 index, const ReceiveListEntry& desc) const {
        memcpy(m_buffer + index, desc.GetData(), desc.GetDataSize());
        return static_cast<s32>(index + (desc.GetDataSize() / sizeof(*m_buffer)));
    }

    s32 Set(s32 index, const u32 val) const {
        memcpy(m_buffer + index, std::addressof(val), sizeof(val));
        return static_cast<s32>(index + (sizeof(val) / sizeof(*m_buffer)));
    }

    Result GetAsyncResult() const {
        MessageHeader hdr(m_buffer);
        MessageHeader null{};
        if (memcmp(hdr.GetData(), null.GetData(), MessageHeader::GetDataSize()) != 0) [[unlikely]] {
            R_SUCCEED();
        }
        return Result(m_buffer[MessageHeader::GetDataSize() / sizeof(*m_buffer)]);
    }

    void SetAsyncResult(Result res) const {
        const s32 index = this->Set(MessageHeader());
        const auto value = res.raw;
        memcpy(m_buffer + index, std::addressof(value), sizeof(value));
    }

    u32 Get32(s32 index) const {
        return m_buffer[index];
    }

    u64 Get64(s32 index) const {
        u64 value;
        memcpy(std::addressof(value), m_buffer + index, sizeof(value));
        return value;
    }

    u64 GetProcessId(s32 index) const {
        return this->Get64(index);
    }

    Handle GetHandle(s32 index) const {
        static_assert(sizeof(Handle) == sizeof(*m_buffer));
        return Handle(m_buffer[index]);
    }

    static constexpr s32 GetSpecialDataIndex(const MessageHeader& hdr, const SpecialHeader& spc) {
        return static_cast<s32>((MessageHeader::GetDataSize() / sizeof(u32)) +
                                (spc.GetHeaderSize() / sizeof(u32)));
    }

    static constexpr s32 GetPointerDescriptorIndex(const MessageHeader& hdr,
                                                   const SpecialHeader& spc) {
        return static_cast<s32>(GetSpecialDataIndex(hdr, spc) + (spc.GetDataSize() / sizeof(u32)));
    }

    static constexpr s32 GetMapAliasDescriptorIndex(const MessageHeader& hdr,
                                                    const SpecialHeader& spc) {
        return GetPointerDescriptorIndex(hdr, spc) +
               static_cast<s32>(hdr.GetPointerCount() * PointerDescriptor::GetDataSize() /
                                sizeof(u32));
    }

    static constexpr s32 GetRawDataIndex(const MessageHeader& hdr, const SpecialHeader& spc) {
        return GetMapAliasDescriptorIndex(hdr, spc) +
               static_cast<s32>(hdr.GetMapAliasCount() * MapAliasDescriptor::GetDataSize() /
                                sizeof(u32));
    }

    static constexpr s32 GetReceiveListIndex(const MessageHeader& hdr, const SpecialHeader& spc) {
        if (const s32 recv_list_index = hdr.GetReceiveListOffset()) {
            return recv_list_index;
        } else {
            return GetRawDataIndex(hdr, spc) + hdr.GetRawCount();
        }
    }

    static constexpr size_t GetMessageBufferSize(const MessageHeader& hdr,
                                                 const SpecialHeader& spc) {
        // Get the size of the plain message.
        size_t msg_size = GetReceiveListIndex(hdr, spc) * sizeof(u32);

        // Add the size of the receive list.
        const auto count = hdr.GetReceiveListCount();
        switch (count) {
        case MessageHeader::ReceiveListCountType_None:
            break;
        case MessageHeader::ReceiveListCountType_ToMessageBuffer:
            break;
        case MessageHeader::ReceiveListCountType_ToSingleBuffer:
            msg_size += ReceiveListEntry::GetDataSize();
            break;
        default:
            msg_size += (static_cast<s32>(count) -
                         static_cast<s32>(MessageHeader::ReceiveListCountType_CountOffset)) *
                        ReceiveListEntry::GetDataSize();
            break;
        }

        return msg_size;
    }
};

} // namespace Kernel
