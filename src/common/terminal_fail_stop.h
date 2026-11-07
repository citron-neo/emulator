// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <limits>

namespace Common {
// Reserved process exit reason, not a recoverable emulator status.
inline constexpr int TerminalDisposalExitCode = 86;
inline constexpr int NoVulkanResult = std::numeric_limits<int>::min();

// Cold terminal failures only. No logger/worker drain, allocation or C++ unwind.
// stage/reason must be static strings. Does not run atexit handlers or destructors.
[[noreturn]] void TerminalFailStop(const char* stage, const char* reason,
                                 int vulkan_result = NoVulkanResult) noexcept;
} // namespace Common
