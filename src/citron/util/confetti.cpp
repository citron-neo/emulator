// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/util/confetti.h"
#include <QPainter>
#include <QRandomGenerator>
#include <cmath>

ConfettiWidget::ConfettiWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    connect(&m_timer, &QTimer::timeout, this, &ConfettiWidget::updateParticles);
    m_timer.setInterval(16); // ~60 FPS
}

void ConfettiWidget::burst() {
    m_particles.clear();
    
    // Left burst
    spawnBurst(QPointF(0, height()), -45.0, 30.0);
    // Right burst
    spawnBurst(QPointF(width(), height()), -135.0, 30.0);

    show();
    raise();
    m_timer.start();
    m_elapsed.start();
}

void ConfettiWidget::spawnBurst(const QPointF& origin, qreal angle_center, qreal angle_spread) {
    auto* rng = QRandomGenerator::global();
    
    for (int i = 0; i < kMaxParticles / 2; ++i) {
        Particle p;
        p.pos = origin;
        
        qreal angle_rad = (angle_center + (rng->generateDouble() * angle_spread * 2.0 - angle_spread)) * M_PI / 180.0;
        qreal speed = 5.0 + rng->generateDouble() * 10.0;
        
        p.velocity = QPointF(std::cos(angle_rad) * speed, std::sin(angle_rad) * speed);
        p.color = QColor::fromHsv(rng->bounded(360), 200, 255);
        p.rotation = rng->generateDouble() * 360.0;
        p.rotation_speed = -10.0 + rng->generateDouble() * 20.0;
        p.opacity = 1.0;
        p.size = rng->bounded(5, 12);
        p.type = rng->bounded(3);
        
        m_particles.push_back(p);
    }
}

void ConfettiWidget::updateParticles() {
    if (m_particles.empty()) {
        m_timer.stop();
        hide();
        return;
    }

    const qreal gravity = 0.2;
    const qreal friction = 0.98;

    for (auto it = m_particles.begin(); it != m_particles.end();) {
        it->velocity.setY(it->velocity.y() + gravity);
        it->velocity *= friction;
        it->pos += it->velocity;
        it->rotation += it->rotation_speed;
        it->opacity -= 0.005;

        if (it->opacity <= 0 || it->pos.y() > height() + 50) {
            it = m_particles.erase(it);
        } else {
            ++it;
        }
    }
    update();
}

void ConfettiWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto& p : m_particles) {
        painter.save();
        painter.setOpacity(p.opacity);
        painter.setBrush(p.color);
        painter.setPen(Qt::NoPen);
        painter.translate(p.pos);
        painter.rotate(p.rotation);

        switch (p.type) {
        case 0: // Square
            painter.drawRect(QRectF(-p.size / 2, -p.size / 2, p.size, p.size / 2));
            break;
        case 1: // Circle
            painter.drawEllipse(QRectF(-p.size / 2, -p.size / 2, p.size, p.size));
            break;
        case 2: // Stripe
            painter.drawRect(QRectF(-p.size / 2, -p.size / 4, p.size, p.size / 4));
            break;
        }
        painter.restore();
    }
}

void ConfettiWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}
