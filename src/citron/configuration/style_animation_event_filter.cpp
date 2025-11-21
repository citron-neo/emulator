// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/style_animation_event_filter.h"
#include <QEvent>
#include <QPropertyAnimation>
#include <QWidget>

StyleAnimationEventFilter::StyleAnimationEventFilter(QObject* parent) : QObject(parent) {}

bool StyleAnimationEventFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        if (auto* widget = qobject_cast<QWidget*>(obj)) {
            // Trigger style update for hover effects
            widget->update();
        }
    }
    return QObject::eventFilter(obj, event);
}
