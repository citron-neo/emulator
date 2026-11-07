// SPDX-License-Identifier: GPL-2.0-or-later
#include <charconv>
#include <cstdlib>
#include "common/terminal_fail_stop.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Common {
[[noreturn]] void TerminalFailStop(const char* stage, const char* reason,
                                 int vulkan_result) noexcept {
    char message[384]{};
    char* cursor = message;
    const auto append = [&](const char* text) {
        while (*text && cursor < message + sizeof(message) - 2) *cursor++ = *text++;
    };
    append("CITRON TERMINAL FAIL-STOP stage=");
    append(stage);
    append(" reason=");
    append(reason);
    append(" vk_result=");
    if (vulkan_result == NoVulkanResult) {
        append("unavailable");
    } else {
        const auto result = std::to_chars(cursor, message + sizeof(message) - 2, vulkan_result);
        if (result.ec == std::errc{}) cursor = result.ptr;
    }
    *cursor++ = '\n';
    // Bypass Common::Log::Stop/its backend locks. In particular, never wait on a
    // failed GPU worker to flush this diagnostic. This is one small direct write.
#ifdef _WIN32
    OutputDebugStringA(message);
    DWORD written{};
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, static_cast<DWORD>(cursor - message),
              &written, nullptr);
#else
    const auto written = write(STDERR_FILENO, message, static_cast<size_t>(cursor - message));
    (void)written;
#endif
    // Crash()/DebugBreak may be resumed by a debugger, and its generic fallback
    // uses exit(). Neither is an unconditional no-unwind lifetime boundary.
    std::_Exit(TerminalDisposalExitCode);
}
} // namespace Common
