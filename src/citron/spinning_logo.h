// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class SpinningLogo : public QWidget {
    Q_OBJECT

public:
    enum class SpinMode {
        None,
        Spinning,
        DragToSpin,
    };
    Q_ENUM(SpinMode)

    explicit SpinningLogo(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pixmap);
    void setSpinMode(SpinMode mode);
    void setSpinMode(int index); // For QComboBox signals

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onTimerTick();

private:
    double angleFromCenter(const QPointF& pos) const;

    QPixmap m_pixmap;
    SpinMode m_mode = SpinMode::None;

    QTimer m_timer;
    double m_angle = 0.0;           // Current rotation angle in degrees
    double m_angularVelocity = 0.0; // Degrees per tick (for DragToSpin)

    // Drag state
    bool m_dragging = false;
    double m_lastMouseAngle = 0.0;
    QElapsedTimer m_dragTimer;
    double m_prevMouseAngle = 0.0;
    qint64 m_prevDragTime = 0;

    static constexpr double kConstantSpeed = 2.0;  // degrees per tick (Spinning mode)
    static constexpr double kFriction = 0.97;       // velocity decay per tick
    static constexpr int kTickMs = 16;              // ~60fps
};
