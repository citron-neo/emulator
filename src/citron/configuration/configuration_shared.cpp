// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <type_traits>
#include <vector>
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configuration_styling.h"

namespace ConfigurationShared {

Tab::Tab(std::shared_ptr<std::vector<Tab*>> group, QWidget* parent) : QWidget(parent) {
    if (group != nullptr) {
        group->push_back(this);
    }
}

Tab::~Tab() = default;

void Tab::ApplyMasterStyle() {
    this->setStyleSheet(ConfigurationStyling::GetMasterStyleSheet());
}

} // namespace ConfigurationShared
