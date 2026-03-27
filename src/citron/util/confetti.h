// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QWidget>
#include <vector>

class ConfettiWidget : public QWidget {
    Q_OBJECT

public:
    explicit ConfettiWidget(QWidget* parent = nullptr);

    void burst(); // Trigger a new burst of confetti

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateParticles();

private:
    struct Particle {
        QPointF pos;
        QPointF velocity;
        QColor color;
        qreal rotation;
        qreal rotation_speed;
        qreal opacity;
        qreal size;
        int type; // 0: square, 1: circle, 2: stripe
    };

    void spawnBurst(const QPointF& origin, qreal angle_center, qreal angle_spread);

    std::vector<Particle> m_particles;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    static constexpr int kMaxParticles = 150;
};
