// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>

class QGraphicsOpacityEffect;

class SetupPage : public QWidget {
    Q_OBJECT

public:
    explicit SetupPage(QWidget* parent = nullptr);
    ~SetupPage() override;

    void FadeIn();

private:
    QGraphicsOpacityEffect* opacity_effect;
};
