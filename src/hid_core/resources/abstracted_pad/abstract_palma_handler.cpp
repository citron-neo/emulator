// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "hid_core/resources/abstracted_pad/abstract_palma_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"

namespace Service::HID {

NpadAbstractPalmaHandler::NpadAbstractPalmaHandler() {}

NpadAbstractPalmaHandler::~NpadAbstractPalmaHandler() = default;

void NpadAbstractPalmaHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractPalmaHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
    return;
}

void NpadAbstractPalmaHandler::SetPalmaResource(PalmaResource* resource) {
    palma_resource = resource;
}

Result NpadAbstractPalmaHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractPalmaHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractPalmaHandler::UpdatePalmaState() {
    // TODO
}

} // namespace Service::HID
