// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdexcept>

namespace VideoCommon {

class InvalidGpuAddress final : public std::runtime_error {
public:
    InvalidGpuAddress() : std::runtime_error{"GPU descriptor outside configured address space"} {}
};

} // namespace VideoCommon
