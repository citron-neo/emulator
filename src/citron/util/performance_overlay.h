// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <memory>
#include <deque>
#include "citron/uisettings.h"

class GMainWindow;

class PerformanceOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceOverlay(GMainWindow* parent);
    ~PerformanceOverlay() override;

    void SetVisible(bool visible);
    bool IsVisible() const { return is_visible; }

public slots:
    void UpdateTheme();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void UpdatePerformanceStats();

private:
    void UpdatePosition();
    void UpdateHardwareTemperatures();
    void DrawPerformanceInfo(QPainter& painter);
    void DrawFrameGraph(QPainter& painter);
    QColor GetFpsColor(double fps) const;
    QColor GetTemperatureColor(float temperature) const;
    QString FormatFps(double fps) const;
    QString FormatFrameTime(double frame_time_ms) const;
    void AddFrameTime(double frame_time_ms);

    GMainWindow* main_window;
    QTimer update_timer;

    // Performance data
    double current_fps = 0.0;
    double current_frame_time = 0.0;
    int shaders_building = 0;
    double emulation_speed = 0.0;
    float cpu_temperature = 0.0f;
    float gpu_temperature = 0.0f;
    QString cpu_sensor_type;
    QString gpu_sensor_type;
    int battery_percentage = 0;
    float battery_temperature = 0.0f;

    // Frame graph data
    static constexpr size_t MAX_FRAME_HISTORY = 120; // 2 seconds at 60 FPS
    std::deque<double> frame_times;
    double min_frame_time = 0.0;
    double max_frame_time = 0.0;
    double avg_frame_time = 0.0;

    // Display settings
    bool is_visible = false;
    QFont title_font;
    QFont value_font;
    QFont small_font;

    // Colors
    QColor background_color;
    QColor border_color;
    QColor text_color;
    QColor fps_color;
    QColor temperature_color;
    QColor graph_background_color;
    QColor graph_line_color;
    QColor graph_fill_color;

    // Layout
    int padding = 12;
    int border_width = 1;
    int corner_radius = 10;
    int graph_height = 40;
    int graph_padding = 8;

    // Drag functionality
    bool is_dragging = false;
    bool has_been_moved = false;
    QPoint drag_start_pos;
    QPoint widget_start_pos;
};
