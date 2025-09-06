// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cctype>
#include <mbedtls/md5.h>

#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_types.h"

namespace Service::BCAT {

// For a name to be valid it must be non-empty, must have a null terminating character as the final
// char, can only contain numbers, letters, underscores and a hyphen if directory and a period if
// file.
constexpr Result VerifyNameValidInternal(std::array<char, 0x20> name, char match_char) {
    const auto null_chars = std::count(name.begin(), name.end(), 0);
    const auto bad_chars = std::count_if(name.begin(), name.end(), [match_char](char c) {
        return !std::isalnum(static_cast<u8>(c)) && c != '_' && c != match_char && c != '\0';
    });
    if (null_chars == 0x20 || null_chars == 0 || bad_chars != 0 || name[0x1F] != '\0') {
        LOG_ERROR(Service_BCAT, "Name passed was invalid!");
        return ResultInvalidArgument;
    }

    return ResultSuccess;
}

constexpr Result VerifyNameValidDir(DirectoryName name) {
    return VerifyNameValidInternal(name, '-');
}

constexpr Result VerifyNameValidFile(FileName name) {
    return VerifyNameValidInternal(name, '.');
}

} // namespace Service::BCAT
