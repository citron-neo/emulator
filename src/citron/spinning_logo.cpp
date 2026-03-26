// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/spinning_logo.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <cmath>

SpinningLogo::SpinningLogo(QWidget* parent) : QWidget(parent) {
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &SpinningLogo::onTimerTick);
    setCursor(Qt::ArrowCursor);
}

void SpinningLogo::setPixmap(const QPixmap& pixmap) {
    m_pixmap = pixmap;
    update();
}

void SpinningLogo::setSpinMode(SpinMode mode) {
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    m_dragging = false;
    m_angularVelocity = 0.0;

    switch (m_mode) {
    case SpinMode::None:
        m_timer.stop();
        m_angle = 0.0;
        setCursor(Qt::ArrowCursor);
        break;
    case SpinMode::Spinning:
        m_timer.start();
        setCursor(Qt::ArrowCursor);
        break;
    case SpinMode::DragToSpin:
        m_timer.start();
        setCursor(Qt::OpenHandCursor);
        break;
    }
    update();
}

void SpinningLogo::setSpinMode(int index) {
    setSpinMode(static_cast<SpinMode>(index));
}

void SpinningLogo::paintEvent(QPaintEvent* /*event*/) {
    if (m_pixmap.isNull()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF target = rect();
    const QPointF center = target.center();

    painter.translate(center);
    painter.rotate(m_angle);
    painter.translate(-center);

    // Scale the pixmap to fit the widget while preserving aspect ratio
    const double scale = qMin(target.width() / m_pixmap.width(),
                               target.height() / m_pixmap.height());
    const double scaledW = m_pixmap.width() * scale;
    const double scaledH = m_pixmap.height() * scale;
    const QRectF destRect(center.x() - scaledW / 2.0, center.y() - scaledH / 2.0,
                           scaledW, scaledH);

    painter.drawPixmap(destRect, m_pixmap, QRectF(m_pixmap.rect()));
}

void SpinningLogo::mousePressEvent(QMouseEvent* event) {
    if (m_mode != SpinMode::DragToSpin) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_angularVelocity = 0.0;
        m_lastMouseAngle = angleFromCenter(event->position());
        m_prevMouseAngle = m_lastMouseAngle;
        m_dragTimer.start();
        m_prevDragTime = 0;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void SpinningLogo::mouseMoveEvent(QMouseEvent* event) {
    if (m_mode != SpinMode::DragToSpin || !m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const double currentAngle = angleFromCenter(event->position());

    // Compute the delta, handling the wrap-around at ±180°
    double delta = currentAngle - m_lastMouseAngle;
    if (delta > 180.0) {
        delta -= 360.0;
    } else if (delta < -180.0) {
        delta += 360.0;
    }

    m_angle += delta;

    // Track velocity using the time elapsed since last sample
    const qint64 now = m_dragTimer.elapsed();
    const qint64 dt = now - m_prevDragTime;
    if (dt > 0) {
        double angleDelta = currentAngle - m_prevMouseAngle;
        if (angleDelta > 180.0) angleDelta -= 360.0;
        if (angleDelta < -180.0) angleDelta += 360.0;
        m_angularVelocity = angleDelta / static_cast<double>(dt) * kTickMs;
        m_prevMouseAngle = currentAngle;
        m_prevDragTime = now;
    }

    m_lastMouseAngle = currentAngle;
    update();
    event->accept();
}

void SpinningLogo::mouseReleaseEvent(QMouseEvent* event) {
    if (m_mode != SpinMode::DragToSpin) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    }
}

void SpinningLogo::onTimerTick() {
    switch (m_mode) {
    case SpinMode::None:
        break;
    case SpinMode::Spinning:
        m_angle += kConstantSpeed;
        if (m_angle >= 360.0) {
            m_angle -= 360.0;
        }
        update();
        break;
    case SpinMode::DragToSpin:
        if (!m_dragging) {
            // Apply friction
            m_angularVelocity *= kFriction;
            if (std::abs(m_angularVelocity) < 0.01) {
                m_angularVelocity = 0.0;
            } else {
                m_angle += m_angularVelocity;
                update();
            }
        }
        break;
    }
}

double SpinningLogo::angleFromCenter(const QPointF& pos) const {
    const QPointF center = QRectF(rect()).center();
    const QPointF delta = pos - center;
    // atan2 returns radians; convert to degrees
    return std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
}
