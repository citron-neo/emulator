// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"

namespace Service::android {

struct ParcelHeader {
    u32 data_size;
    u32 data_offset;
    u32 objects_size;
    u32 objects_offset;
};
static_assert(sizeof(ParcelHeader) == 16, "ParcelHeader has wrong size");

class InputParcel final {
public:
    explicit InputParcel(std::span<const u8> in_data) : read_buffer(std::move(in_data)) {
        DeserializeHeader();
        [[maybe_unused]] const std::u16string token = ReadInterfaceToken();
    }

    template <typename T>
    void Read(T& val) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable.");
        ASSERT(read_index + sizeof(T) <= read_buffer.size());

        std::memcpy(&val, read_buffer.data() + read_index, sizeof(T));
        read_index += sizeof(T);
        read_index = Common::AlignUp(read_index, 4);
    }

    template <typename T>
    T Read() {
        T val;
        Read(val);
        return val;
    }

    template <typename T>
    void ReadFlattened(T& val) {
        const auto flattened_size = Read<s64>();
        ASSERT(sizeof(T) == flattened_size);
        Read(val);
    }

    template <typename T>
    T ReadFlattened() {
        T val;
        ReadFlattened(val);
        return val;
    }

    template <typename T>
    T ReadUnaligned() {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable.");
        ASSERT(read_index + sizeof(T) <= read_buffer.size());

        T val;
        std::memcpy(&val, read_buffer.data() + read_index, sizeof(T));
        read_index += sizeof(T);
        return val;
    }

    template <typename T>
    const std::shared_ptr<T> ReadObject() {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable.");

        const auto is_valid{Read<bool>()};

        if (is_valid) {
            auto result = std::make_shared<T>();
            ReadFlattened(*result);
            return result;
        }

        return {};
    }

    std::u16string ReadInterfaceToken() {
        [[maybe_unused]] const u32 unknown = Read<u32>();
        const u32 length = Read<u32>();

        std::u16string token;
        token.reserve(length + 1);

        for (u32 ch = 0; ch < length + 1; ++ch) {
            token.push_back(ReadUnaligned<u16>());
        }

        read_index = Common::AlignUp(read_index, 4);

        return token;
    }

    void DeserializeHeader() {
        ASSERT(read_buffer.size() > sizeof(ParcelHeader));

        ParcelHeader header{};
        std::memcpy(&header, read_buffer.data(), sizeof(ParcelHeader));

        read_index = header.data_offset;
    }

private:
    std::span<const u8> read_buffer;
    std::size_t read_index = 0;
};

class OutputParcel final {
public:
    OutputParcel() = default;

    template <typename T>
    void Write(const T& val) {
        this->WriteImpl(val, m_data_buffer);
    }

    template <typename T>
    void WriteFlattenedObject(const T* ptr) {
        if (!ptr) {
            this->Write<u32>(0);
            return;
        }

        this->Write<u32>(1);
        this->Write<s64>(sizeof(T));
        this->Write(*ptr);
    }

    template <typename T>
    void WriteFlattenedObject(const std::shared_ptr<T> ptr) {
        this->WriteFlattenedObject(ptr.get());
    }

    template <typename T>
    void WriteInterface(const T& val) {
        this->WriteImpl(val, m_data_buffer);
        this->WriteImpl(0U, m_object_buffer);
    }

    std::span<u8> Serialize() {
        m_output_buffer.resize(sizeof(ParcelHeader) + m_data_buffer.size() +
                               m_object_buffer.size());

        ParcelHeader header{};
        header.data_size = static_cast<u32>(m_data_buffer.size());
        header.data_offset = sizeof(ParcelHeader);
        header.objects_size = static_cast<u32>(m_object_buffer.size());
        header.objects_offset = header.data_offset + header.data_size;

        std::memcpy(m_output_buffer.data(), &header, sizeof(ParcelHeader));
        std::ranges::copy(m_data_buffer, m_output_buffer.data() + header.data_offset);
        std::ranges::copy(m_object_buffer, m_output_buffer.data() + header.objects_offset);

        return m_output_buffer;
    }

private:
    template <typename T, size_t BufferSize>
        requires(std::is_trivially_copyable_v<T>)
    void WriteImpl(const T& val, boost::container::small_vector<u8, BufferSize>& buffer) {
        const size_t aligned_size = Common::AlignUp(sizeof(T), 4);
        const size_t old_size = buffer.size();
        buffer.resize(old_size + aligned_size);

        std::memcpy(buffer.data() + old_size, &val, sizeof(T));
    }

private:
    boost::container::small_vector<u8, 0x1B0> m_data_buffer;
    boost::container::small_vector<u8, 0x40> m_object_buffer;
    boost::container::small_vector<u8, 0x200> m_output_buffer;
};

} // namespace Service::android
