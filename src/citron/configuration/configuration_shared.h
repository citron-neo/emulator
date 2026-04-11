// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QString>
#include <QWidget>
#include <qobjectdefs.h>

class QObject;

namespace ConfigurationShared {

class Tab : public QWidget {
    Q_OBJECT

public:
    explicit Tab(std::shared_ptr<std::vector<Tab*>> group, QWidget* parent = nullptr);
    ~Tab();

    virtual void ApplyConfiguration() = 0;
    virtual void SetConfiguration() = 0;

    /**
     * Applies the master unified stylesheet to this tab.
     * This ensures all tabs (General, CPU, etc.) follow the same high-fidelity design.
     */
    void ApplyMasterStyle();
};

} // namespace ConfigurationShared
