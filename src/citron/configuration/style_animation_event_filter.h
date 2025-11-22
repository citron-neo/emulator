// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QHash>

class QPropertyAnimation;
class QPushButton;

class StyleAnimationEventFilter final : public QObject {
    Q_OBJECT

public:
    explicit StyleAnimationEventFilter(QObject* parent = nullptr);
    ~StyleAnimationEventFilter() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void animatePadding(QPushButton* button, int end);

    QHash<QPushButton*, QPropertyAnimation*> animations;
};
