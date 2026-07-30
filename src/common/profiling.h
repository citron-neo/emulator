// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Optional Tracy instrumentation. When CITRON_ENABLE_TRACY is off (the default),
// every macro below expands to nothing — no Tracy headers, no client code, no
// runtime overhead.

#if defined(CITRON_ENABLE_TRACY) && CITRON_ENABLE_TRACY

 #    ifndef TRACY_ENABLE
 #        define TRACY_ENABLE
 #    endif
 #    ifndef TRACY_FIBERS
 #        define TRACY_FIBERS
 #    endif
 // NOTE: We deliberately do NOT define TRACY_CALLSTACK here. Tracy's
 // ZoneScoped/ZoneScopedN macros (what CITRON_PROFILE_SCOPE expands to) pass
 // TRACY_CALLSTACK straight through as the callstack-capture depth on every
 // single call. If it's defined project-wide, every zone everywhere -- including
 // very hot ones like Kernel::Svc::Call and KScheduler::ScheduleImpl -- captures
 // a full native stack walk on every invocation instead of a cheap timestamp.
 // Tracy's own header falls back to TRACY_CALLSTACK 0 (no capture) when this is
 // left undefined, which is what we want by default. Use
 // CITRON_PROFILE_SCOPE_CS(name, depth) below at a specific call site if that
 // one zone genuinely needs callstack capture.

 #    include <tracy/Tracy.hpp>

 #    define CITRON_PROFILE_SCOPE(name) ZoneScopedN(name)
 #    define CITRON_PROFILE_SCOPE_CS(name, depth) ZoneScopedNS(name, depth)
 #    define CITRON_PROFILE_FRAME_MARK() FrameMark
 #    define CITRON_PROFILE_FRAME_MARK_N(name) FrameMarkNamed(name)
 #    define CITRON_PROFILE_PLOT(name, value) TracyPlot(name, value)

 // Wraps a mutex type so Tracy tracks acquire/wait/hold time on its own timeline
 // lane. Declare in place of the plain type, e.g.:
 //   CITRON_PROFILE_LOCKABLE(std::mutex, list_lock);
 // The resulting variable is still a valid std::mutex-compatible lockable (works
 // with std::scoped_lock/std::unique_lock as normal) when Tracy is enabled, and
 // is the untouched plain type with zero overhead when it's not.
 #    define CITRON_PROFILE_LOCKABLE(type, varname) TracyLockable(type, varname)

 #    if defined(CITRON_ENABLE_TRACY_MEMORY) && CITRON_ENABLE_TRACY_MEMORY
 #        define CITRON_PROFILE_MEM_SCOPE(name) CITRON_PROFILE_SCOPE(name)
 #    else
 #        define CITRON_PROFILE_MEM_SCOPE(name)
 #    endif

 #else

 #    define CITRON_PROFILE_SCOPE(name)
 #    define CITRON_PROFILE_SCOPE_CS(name, depth)
 #    define CITRON_PROFILE_FRAME_MARK()
 #    define CITRON_PROFILE_FRAME_MARK_N(name)
 #    define CITRON_PROFILE_MEM_SCOPE(name)
 #    define CITRON_PROFILE_PLOT(name, value)
 #    define CITRON_PROFILE_LOCKABLE(type, varname) type varname

 #endif
