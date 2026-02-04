// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/receiver.h"


namespace Service::PSC {

IReceiver::IReceiver(Core::System& system_)
    : ServiceFramework{system_, "IReceiver"}, service_context{system_, "IReceiver"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IReceiver::AddSource>, "AddSource"},
            {1, D<&IReceiver::RemoveSource>, "RemoveSource"},
            {2, D<&IReceiver::GetReceiveEventHandle>, "GetReceiveEventHandle"},
            {3, D<&IReceiver::Receive>, "Receive"},
            {4, D<&IReceiver::ReceiveWithTick>, "ReceiveWithTick"},
        };
    // clang-format on

    RegisterHandlers(functions);

    receive_event = service_context.CreateEvent("IReceiver::ReceiveEvent");
}

IReceiver::~IReceiver() {
    service_context.CloseEvent(receive_event);
}

Result IReceiver::AddSource(SourceName source_name) {
    const std::string name = source_name.GetString();
    LOG_INFO(Service_PSC, "called: source_name={}", name);

    // Add source if it doesn't already exist
    if (message_sources.find(name) == message_sources.end()) {
        message_sources[name] = {};
    }

    R_SUCCEED();
}

Result IReceiver::RemoveSource(SourceName source_name) {
    const std::string name = source_name.GetString();
    LOG_INFO(Service_PSC, "called: source_name={}", name);

    // Remove source if it exists
    message_sources.erase(name);

    R_SUCCEED();
}

Result IReceiver::GetReceiveEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PSC, "called");
    *out_event = &receive_event->GetReadableEvent();
    R_SUCCEED();
}

Result IReceiver::Receive(Out<OverlayNotification> out_notification, Out<MessageFlags> out_flags) {
    u64 tick;
    return ReceiveWithTick(out_notification, out_flags, Out<u64>(&tick));
}

Result IReceiver::ReceiveWithTick(Out<OverlayNotification> out_notification,
                                  Out<MessageFlags> out_flags, Out<u64> out_tick) {
    LOG_DEBUG(Service_PSC, "called");

    // Find the message with the lowest ID across all sources
    const std::string* target_source = nullptr;
    size_t target_index = 0;

    for (const auto& [source_name, messages] : message_sources) {
        if (!messages.empty()) {
            if (target_source == nullptr) {
                target_source = &source_name;
                target_index = 0;
            }
            // Note: In the real implementation, we would track message IDs
            // For now, just use FIFO order
        }
    }

    if (target_source != nullptr) {
        auto& messages = message_sources[*target_source];
        *out_notification = messages[target_index].first;
        *out_flags = messages[target_index].second;
        *out_tick = 0; // TODO: Implement tick tracking

        // Remove the message
        messages.erase(messages.begin() + target_index);

        // Clear event if no more messages
        bool has_messages = false;
        for (const auto& [_, msgs] : message_sources) {
            if (!msgs.empty()) {
                has_messages = true;
                break;
            }
        }
        if (!has_messages) {
            receive_event->Clear();
        }

        R_SUCCEED();
    }

    // No messages available
    *out_notification = {};
    *out_flags = {};
    *out_tick = 0;

    LOG_WARNING(Service_PSC, "No messages available");
    R_THROW(ResultUnknown); // TODO: Use proper OvlnResult::NoMessages when available
}

} // namespace Service::PSC
