// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QButtonGroup>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/qtconcurrentrun.h>
#include "common/logging/log.h"
#include "ui_compatdb.h"
#include "citron/compatdb.h"

CompatDB::CompatDB(QWidget* parent)
    : QWizard(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::CompatDB>()} {
    ui->setupUi(this);

    connect(ui->radioButton_GameBoot_Yes, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_GameBoot_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Gameplay_Yes, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Gameplay_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_NoFreeze_Yes, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_NoFreeze_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Complete_Yes, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Complete_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Graphical_Major, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Graphical_Minor, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Graphical_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Audio_Major, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Audio_Minor, &QRadioButton::clicked, this, &CompatDB::EnableNext);
    connect(ui->radioButton_Audio_No, &QRadioButton::clicked, this, &CompatDB::EnableNext);

    connect(button(NextButton), &QPushButton::clicked, this, &CompatDB::Submit);
    connect(&testcase_watcher, &QFutureWatcher<bool>::finished, this,
            &CompatDB::OnTestcaseSubmitted);
}

CompatDB::~CompatDB() = default;

enum class CompatDBPage {
    Intro = 0,
    GameBoot = 1,
    GamePlay = 2,
    Freeze = 3,
    Completion = 4,
    Graphical = 5,
    Audio = 6,
    Final = 7,
};

void CompatDB::Submit() {

}

int CompatDB::nextId() const {
    switch ((static_cast<CompatDBPage>(currentId()))) {
    case CompatDBPage::Intro:
        return static_cast<int>(CompatDBPage::GameBoot);
    case CompatDBPage::GameBoot:
        if (ui->radioButton_GameBoot_No->isChecked()) {
            return static_cast<int>(CompatDBPage::Final);
        }
        return static_cast<int>(CompatDBPage::GamePlay);
    case CompatDBPage::GamePlay:
        if (ui->radioButton_Gameplay_No->isChecked()) {
            return static_cast<int>(CompatDBPage::Final);
        }
        return static_cast<int>(CompatDBPage::Freeze);
    case CompatDBPage::Freeze:
        if (ui->radioButton_NoFreeze_No->isChecked()) {
            return static_cast<int>(CompatDBPage::Final);
        }
        return static_cast<int>(CompatDBPage::Completion);
    case CompatDBPage::Completion:
        if (ui->radioButton_Complete_No->isChecked()) {
            return static_cast<int>(CompatDBPage::Final);
        }
        return static_cast<int>(CompatDBPage::Graphical);
    case CompatDBPage::Graphical:
        return static_cast<int>(CompatDBPage::Audio);
    case CompatDBPage::Audio:
        return static_cast<int>(CompatDBPage::Final);
    case CompatDBPage::Final:
        return -1;
    default:
        LOG_ERROR(Frontend, "Unexpected page: {}", currentId());
        return static_cast<int>(CompatDBPage::Intro);
    }
}

CompatibilityStatus CompatDB::CalculateCompatibility() const {
    if (ui->radioButton_GameBoot_No->isChecked()) {
        return CompatibilityStatus::WontBoot;
    }

    if (ui->radioButton_Gameplay_No->isChecked()) {
        return CompatibilityStatus::IntroMenu;
    }

    if (ui->radioButton_NoFreeze_No->isChecked() || ui->radioButton_Complete_No->isChecked()) {
        return CompatibilityStatus::Ingame;
    }

    if (ui->radioButton_Graphical_Major->isChecked() || ui->radioButton_Audio_Major->isChecked()) {
        return CompatibilityStatus::Ingame;
    }

    if (ui->radioButton_Graphical_Minor->isChecked() || ui->radioButton_Audio_Minor->isChecked()) {
        return CompatibilityStatus::Playable;
    }

    return CompatibilityStatus::Perfect;
}

void CompatDB::OnTestcaseSubmitted() {
    if (!testcase_watcher.result()) {
        QMessageBox::critical(this, tr("Communication error"),
                              tr("An error occurred while sending the Testcase"));
        button(NextButton)->setEnabled(true);
        button(NextButton)->setText(tr("Next"));
        button(CancelButton)->setVisible(true);
    } else {
        next();
        // older versions of QT don't support the "NoCancelButtonOnLastPage" option, this is a
        // workaround
        button(CancelButton)->setVisible(false);
    }
}

void CompatDB::EnableNext() {
    button(NextButton)->setEnabled(true);
}
