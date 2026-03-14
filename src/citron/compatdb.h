// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QFutureWatcher>
#include <QWizard>

namespace Ui {
class CompatDB;
}

enum class CompatibilityStatus {
    Perfect = 0,
    Playable = 1,
    // Unused: Okay = 2,
    Ingame = 3,
    IntroMenu = 4,
    WontBoot = 5,
};

class CompatDB : public QWizard {
    Q_OBJECT

public:
    explicit CompatDB(QWidget* parent = nullptr);
    ~CompatDB();
    int nextId() const override;

private:
    QFutureWatcher<bool> testcase_watcher;

    std::unique_ptr<Ui::CompatDB> ui;

    void Submit();
    CompatibilityStatus CalculateCompatibility() const;
    void OnTestcaseSubmitted();
    void EnableNext();
};
