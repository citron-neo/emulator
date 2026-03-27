// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <random>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include "citron/util/plinko_widget.h"

PlinkoWidget::PlinkoWidget(QWidget* parent) : QWidget(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PlinkoWidget::updatePhysics);
    setupPegs();
}

void PlinkoWidget::setupPegs() {
    m_pegs.clear();
    const int rows = 18;
    const float widget_width = width() > 0 ? width() : 850.0f;
    const float widget_height = height() > 0 ? height() : 640.0f;
    const float row_height = (widget_height - 180) / rows;
    
    // Middle-ground density: 52px spacing is the sweet spot for a 9px ball
    const int cols = std::max(10, static_cast<int>(widget_width / 52.0f)); 
    const float spacing_x = (widget_width - 100) / (cols - 1);
    const float margin_x = 50.0f;
    
    for (int r = 0; r < rows; ++r) {
        float offset_x = margin_x + ((r % 2 == 1) ? (spacing_x / 2.0f) : 0);
        int current_cols = (r % 2 == 1) ? (cols - 1) : cols;
        
        for (int c = 0; c < current_cols; ++c) {
            float x = offset_x + c * spacing_x;
            float y = 70 + r * row_height;
            
            if (x > 35 && x < widget_width - 35) {
                m_pegs.push_back({QPointF(x, y), 4.0});
            }
        }
    }
}

void PlinkoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    setupPegs();
    if (!m_is_animating) {
        m_ball_pos.setX(width() / 2.0);
    }
}

void PlinkoWidget::updatePhysics() {
    if (!m_is_animating)
        return;

    const qreal gravity = 0.15;
    const qreal friction = 0.995;
    const qreal bounce = 0.55;
    const qreal ball_radius = 9.0;

    m_ball_vel.setY(m_ball_vel.y() + gravity);
    m_ball_vel *= friction;
    
    // Constant 'Jerkiness': Subtle random horizontal wind in every frame
    static std::random_device rd_frame;
    static std::mt19937 gen_frame(rd_frame());
    std::uniform_real_distribution<qreal> wind_dist(-0.25, 0.25);
    m_ball_vel.setX(m_ball_vel.x() + wind_dist(gen_frame));
    
    m_ball_pos += m_ball_vel;

    // Enhanced Zigzag Side Bumper Physics
    const qreal wall_base_x = 30.0;
    const qreal tooth_height = 40.0;
    const qreal tooth_depth = 15.0;
    
    auto handleWall = [&](qreal ball_x, qreal ball_y, bool left) {
        qreal rel_y = std::fmod(ball_y, tooth_height);
        qreal current_tooth_x = wall_base_x + (left ? 1.0 : -1.0) * tooth_depth * (1.0 - std::abs(rel_y - tooth_height/2.0) / (tooth_height/2.0));
        
        bool collision = left ? (ball_x < current_tooth_x + ball_radius) : (ball_x > width() - (current_tooth_x + ball_radius));
        if (collision) {
            m_ball_pos.setX(left ? current_tooth_x + ball_radius : width() - (current_tooth_x + ball_radius));
            m_ball_vel.setX((left ? 1.0 : -1.0) * (std::abs(m_ball_vel.x()) * bounce + 2.0));
            m_ball_vel.setY(m_ball_vel.y() * bounce - 0.5); 
        }
    };

    handleWall(m_ball_pos.x(), m_ball_pos.y(), true);
    handleWall(m_ball_pos.x(), m_ball_pos.y(), false);

    // Peg collisions
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<qreal> jitter(-0.8, 0.8);

    for (const auto& peg : m_pegs) {
        QPointF diff = m_ball_pos - peg.pos;
        qreal distSq = diff.x() * diff.x() + diff.y() * diff.y();
        qreal minDist = ball_radius + peg.radius;
        if (distSq < minDist * minDist) {
            qreal dist = std::sqrt(distSq);
            QPointF normal = diff / dist;
            m_ball_pos = peg.pos + normal * (minDist + 0.5);

            // Reflect velocity with strong RNG 'Jerk'
            qreal dot = m_ball_vel.x() * normal.x() + m_ball_vel.y() * normal.y();
            m_ball_vel = (m_ball_vel - 2 * dot * normal) * bounce;
            
            // Stronger horizontal kick on collision
            std::uniform_real_distribution<qreal> collision_kick(-2.5, 2.5);
            m_ball_vel.setX(m_ball_vel.x() + collision_kick(gen));
            
            if (normal.y() < -0.1 && m_ball_vel.y() < 1.0) {
                m_ball_vel.setY(m_ball_vel.y() - 0.5);
            }
        }
    }

    // Bin check
    if (m_ball_pos.y() > height() - 110) {
        m_is_animating = false;
        m_timer->stop();

        float bin_width = (float)width() / 5.0f;
        int bin_index = std::clamp(static_cast<int>(m_ball_pos.x() / bin_width), 0, 4);
        emit gameSelected(bin_index);
    }

    update();
}

void PlinkoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Background
    QLinearGradient bg_grad(0, 0, 0, height());
    bg_grad.setColorAt(0, QColor(25, 25, 35));
    bg_grad.setColorAt(1, QColor(35, 35, 50));
    painter.fillRect(rect(), bg_grad);

    // Side Zigzag Bumpers
    painter.setBrush(QColor(50, 50, 70));
    painter.setPen(QPen(QColor(100, 100, 150), 2));

    auto drawBumpers = [&](qreal x, bool left) {
        QPainterPath path;
        path.moveTo(x, 0);
        for (int i = 0; i < height() - 120; i += 40) {
            path.lineTo(left ? x + 15 : x - 15, i + 20);
            path.lineTo(x, i + 40);
        }
        path.lineTo(x, height() - 110);
        path.lineTo(left ? 0 : width(), height() - 110);
        path.lineTo(left ? 0 : width(), 0);
        path.closeSubpath();
        painter.drawPath(path);
    };
    drawBumpers(30, true);
    drawBumpers(width() - 30, false);

    // Pegs (Dense Hexagonal Grid)
    for (const auto& peg : m_pegs) {
        QRadialGradient grad(peg.pos, peg.radius);
        grad.setColorAt(0, QColor(255, 255, 255));
        grad.setColorAt(1, QColor(100, 100, 120));
        painter.setBrush(grad);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(peg.pos, peg.radius, peg.radius);
    }

    // Bins
    float bin_width = (float)width() / 5.0f;
    for (int i = 0; i < 5; ++i) {
        float x = i * bin_width;
        QRectF bin_rect(x, height() - 110, bin_width, 110);

        if (!m_is_animating && m_ball_pos.y() > height() - 150 &&
            (int)(m_ball_pos.x() / bin_width) == i) {
            painter.fillRect(bin_rect, QColor(255, 255, 255, 30));
        }

        painter.setPen(QPen(QColor(180, 180, 220, 60), 2));
        painter.drawLine(x, height() - 120, x, height());

        // High Quality Icons
        if (i < (int)m_game_images.size()) {
            int icon_dim = 80;
            QRectF icon_rect(x + (bin_width - icon_dim) / 2.0f, height() - 100, icon_dim, icon_dim);
            painter.drawImage(icon_rect, m_game_images[i]);
        }
    }
    painter.drawLine(width(), height() - 120, width(), height());

    // Ball
    if (m_is_animating || m_ball_pos.y() < 60) {
        QRadialGradient ball_grad(m_ball_pos, 9);
        ball_grad.setColorAt(0, QColor(255, 200, 200));
        ball_grad.setColorAt(1, QColor(200, 50, 50));
        painter.setBrush(ball_grad);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(m_ball_pos, 9, 9);
    }
}

void PlinkoWidget::mousePressEvent(QMouseEvent* event) {
    if (m_is_animating || !m_can_drop)
        return;

    if (event->button() == Qt::LeftButton) {
        m_ball_pos = QPointF(event->position().x(), 40);
        
        // Add tiny random horizontal kick to prevent 'perfect' vertical drops
        std::random_device rd; // Local declaration for now, ideally a member variable
        std::mt19937 gen(rd());
        std::uniform_real_distribution<qreal> kick(-1.5, 1.5);
        m_ball_vel = QPointF(kick(gen), 0);
        
        m_is_animating = true;
        m_can_drop = false; 
        m_timer->start(16);
        update();
    }
}

void PlinkoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_is_animating && m_can_drop) {
        m_ball_pos.setX(event->position().x());
        update();
    }
}
