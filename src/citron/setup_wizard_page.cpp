// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include "citron/setup_wizard_page.h"

SetupPage::SetupPage(QWidget* parent) : QWidget(parent) {
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1.0);
    setGraphicsEffect(opacity_effect);
}

SetupPage::~SetupPage() = default;

void SetupPage::FadeIn() {
    auto* animation = new QPropertyAnimation(opacity_effect, "opacity");
    animation->setDuration(500);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

#include "setup_wizard_page.moc"
