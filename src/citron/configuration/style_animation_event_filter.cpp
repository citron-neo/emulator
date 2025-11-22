// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/style_animation_event_filter.h"
#include <QEvent>
#include <QPushButton>
#include <QPropertyAnimation>

// A helper class that owns the property being animated.
class PaddingAnimator : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padding READ GetPadding WRITE SetPadding)

public:
    explicit PaddingAnimator(QPushButton* button, int initial_padding)
    : QObject(button), target_button(button), current_padding(initial_padding) {}

    void SetPadding(int padding) {
        current_padding = padding;
        target_button->setStyleSheet(QStringLiteral("padding-left: %1px;").arg(padding));
    }

    int GetPadding() const { return current_padding; }

private:
    QPushButton* target_button;
    int current_padding;
};

StyleAnimationEventFilter::StyleAnimationEventFilter(QObject* parent) : QObject(parent) {}
StyleAnimationEventFilter::~StyleAnimationEventFilter() = default;

void StyleAnimationEventFilter::animatePadding(QPushButton* button, int end) {
    if (animations.contains(button)) {
        animations.value(button)->stop();
    }

    int start_padding = 10; // Assume a default start padding
    // A better approach would be to parse the current stylesheet, but this is safer for now.
    if (end == 10) { // If shrinking, start from the expanded state
        start_padding = 14;
    }

    auto* animator = new PaddingAnimator(button, start_padding);
    auto* animation = new QPropertyAnimation(animator, "padding", this);

    animation->setDuration(150);
    animation->setStartValue(start_padding);
    animation->setEndValue(end);
    animation->setEasingCurve(QEasingCurve::OutQuad);

    animations.insert(button, animation);

    connect(animation, &QPropertyAnimation::finished, this, [this, button](){
        animations.remove(button);
    });

    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

bool StyleAnimationEventFilter::eventFilter(QObject* watched, QEvent* event) {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (!button) {
        return false;
    }

    if (event->type() == QEvent::Enter) {
        animatePadding(button, 14); // Grow
    } else if (event->type() == QEvent::Leave) {
        animatePadding(button, 10); // Shrink
    }
    return QObject::eventFilter(watched, event);
}

#include "citron/configuration/style_animation_event_filter.moc"
