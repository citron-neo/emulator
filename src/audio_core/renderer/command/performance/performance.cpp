// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/performance/performance.h"
#include "core/core.h"
#include "core/core_timing.h"

namespace AudioCore::Renderer {

void PerformanceCommand::Dump([[maybe_unused]] const AudioRenderer::CommandListProcessor& processor,
                              std::string& string) {
    string += fmt::format("PerformanceCommand\n\tstate {}\n", static_cast<u32>(state));
}

void PerformanceCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    auto base{entry_address.translated_address};
    if (state == PerformanceState::Start) {
        auto start_time_ptr{reinterpret_cast<u32*>(base + entry_address.entry_start_time_offset)};
        *start_time_ptr =
            static_cast<u32>(processor.system->CoreTiming().GetGlobalTimeUs().count() -
                             processor.start_time - processor.current_processing_time);
    } else if (state == PerformanceState::Stop) {
        auto processed_time_ptr{
            reinterpret_cast<u32*>(base + entry_address.entry_processed_time_offset)};
        auto entry_count_ptr{
            reinterpret_cast<u32*>(base + entry_address.header_entry_count_offset)};

        *processed_time_ptr =
            static_cast<u32>(processor.system->CoreTiming().GetGlobalTimeUs().count() -
                             processor.start_time - processor.current_processing_time);
        (*entry_count_ptr)++;
    }
}

bool PerformanceCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
