// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>
#include <numeric>
#include <cstdlib>

#include "citron/main.h"
#include "citron/util/performance_overlay.h"
#include "core/core.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

PerformanceOverlay::PerformanceOverlay(GMainWindow* parent)
    : QWidget(parent), main_window(parent) {

    // Set up the widget properties
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);

    // Initialize fonts with better typography
    title_font = QFont(QString::fromUtf8("Segoe UI"), 9, QFont::Medium);
    value_font = QFont(QString::fromUtf8("Segoe UI"), 11, QFont::Bold);
    small_font = QFont(QString::fromUtf8("Segoe UI"), 8, QFont::Normal);

    // Initialize colors with a more modern palette
    background_color = QColor(20, 20, 20, 180);  // Darker, more opaque background
    border_color = QColor(60, 60, 60, 120);      // Subtle border
    text_color = QColor(220, 220, 220, 255);     // Light gray text
    fps_color = QColor(76, 175, 80, 255);        // Material Design green

    // Graph colors
    graph_background_color = QColor(40, 40, 40, 100);
    graph_line_color = QColor(76, 175, 80, 200);
    graph_fill_color = QColor(76, 175, 80, 60);

    // Set up timer for updates
    update_timer.setSingleShot(false);
    connect(&update_timer, &QTimer::timeout, this, &PerformanceOverlay::UpdatePerformanceStats);

    // Set initial size - larger to accommodate the graph
    resize(220, 180);

    // Position in top-left corner
    UpdatePosition();
}

PerformanceOverlay::~PerformanceOverlay() = default;

void PerformanceOverlay::SetVisible(bool visible) {
    if (is_visible == visible) {
        return;
    }

    is_visible = visible;

    if (visible) {
        show();
        update_timer.start(500); // Update every 500ms for more accurate data
    } else {
        hide();
        update_timer.stop();
    }
}

void PerformanceOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Draw background with rounded corners and subtle shadow effect
    QPainterPath background_path;
    background_path.addRoundedRect(rect(), corner_radius, corner_radius);

    // Draw subtle shadow
    QPainterPath shadow_path = background_path.translated(1, 1);
    painter.fillPath(shadow_path, QColor(0, 0, 0, 40));

    // Draw main background
    painter.fillPath(background_path, background_color);

    // Draw subtle border
    painter.setPen(QPen(border_color, border_width));
    painter.drawPath(background_path);

    // Draw performance information
    DrawPerformanceInfo(painter);

    // Draw frame graph
    DrawFrameGraph(painter);
}

void PerformanceOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdatePosition();
}

void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = true;
        drag_start_pos = event->globalPosition().toPoint();
        widget_start_pos = this->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (is_dragging) {
        QPoint delta = event->globalPosition().toPoint() - drag_start_pos;
        move(widget_start_pos + delta);
    }
    QWidget::mouseMoveEvent(event);
}

void PerformanceOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = false;
        has_been_moved = true;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void PerformanceOverlay::UpdatePerformanceStats() {
    if (!main_window) {
        return;
    }

    // Get shader building info (this is safe to call)
    shaders_building = main_window->GetShadersBuilding();

    // Use a static counter to only call the performance methods occasionally
    // This reduces the chance of conflicts with the status bar updates
    static int update_counter = 0;
    update_counter++;

    // Try to get performance data every 2nd update (every 1 second)
    if (update_counter % 2 == 0) {
        try {
            current_fps = main_window->GetCurrentFPS();
            current_frame_time = main_window->GetCurrentFrameTime();
            emulation_speed = main_window->GetEmulationSpeed();

            // Validate the values
            if (std::isnan(current_fps) || current_fps < 0.0 || current_fps > 1000.0) {
                current_fps = 60.0;
            }
            if (std::isnan(current_frame_time) || current_frame_time < 0.0 || current_frame_time > 100.0) {
                current_frame_time = 16.67;
            }
            if (std::isnan(emulation_speed) || emulation_speed < 0.0 || emulation_speed > 1000.0) {
                emulation_speed = 100.0;
            }

            // Ensure FPS and frame time are consistent
            if (current_fps > 0.0 && current_frame_time > 0.0) {
                // Recalculate frame time from FPS to ensure consistency
                current_frame_time = 1000.0 / current_fps;
            }
        } catch (...) {
            // If we get an exception, use the last known good values
            // Don't reset to defaults immediately
        }
    }

    // If we don't have valid data yet, use defaults
    if (std::isnan(current_fps) || current_fps <= 0.0) {
        current_fps = 60.0;
    }
    if (std::isnan(current_frame_time) || current_frame_time <= 0.0) {
        current_frame_time = 16.67; // 60 FPS
    }
    if (std::isnan(emulation_speed) || emulation_speed <= 0.0) {
        emulation_speed = 100.0;
    }

    // Add frame time to graph history (only if it's valid)
    if (current_frame_time > 0.0) {
        AddFrameTime(current_frame_time);
    }

    // Update FPS color based on performance
    fps_color = GetFpsColor(current_fps);

    // Trigger a repaint
    update();
}

void PerformanceOverlay::UpdatePosition() {
    if (!main_window) {
        return;
    }

    // Only position in top-left corner if we haven't been moved by the user
    if (!has_been_moved) {
        QPoint main_window_pos = main_window->mapToGlobal(QPoint(0, 0));
        move(main_window_pos.x() + 10, main_window_pos.y() + 10);
    }
}

void PerformanceOverlay::DrawPerformanceInfo(QPainter& painter) {
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    int y_offset = padding + 12;
    const int line_height = 22;
    const int section_spacing = 4;

    // Draw title with subtle styling
    painter.setFont(title_font);
    painter.setPen(text_color);
    painter.drawText(padding, y_offset, QString::fromUtf8("CITRON"));
    y_offset += line_height + section_spacing;

    // Draw FPS with larger, more prominent display
    painter.setFont(value_font);
    painter.setPen(fps_color);
    QString fps_text = QString::fromUtf8("%1 FPS").arg(FormatFps(current_fps));
    painter.drawText(padding, y_offset, fps_text);
    y_offset += line_height;

    // Draw frame time
    painter.setFont(small_font);
    painter.setPen(text_color);
    QString frame_time_text = QString::fromUtf8("Frame: %1 ms").arg(FormatFrameTime(current_frame_time));
    painter.drawText(padding, y_offset, frame_time_text);
    y_offset += line_height - 2;

    // Draw emulation speed
    QString speed_text = QString::fromUtf8("Speed: %1%").arg(emulation_speed, 0, 'f', 0);
    painter.drawText(padding, y_offset, speed_text);
    y_offset += line_height - 2;

    // Draw shader building info with accent color
    if (shaders_building > 0) {
        painter.setPen(QColor(255, 152, 0, 255)); // Material Design orange
        QString shader_text = QString::fromUtf8("Building: %1 shader(s)").arg(shaders_building);
        painter.drawText(padding, y_offset, shader_text);
    }
}

void PerformanceOverlay::DrawFrameGraph(QPainter& painter) {
    if (frame_times.empty()) {
        return;
    }

    const int graph_y = height() - graph_height - padding;
    const int graph_width = width() - (padding * 2);
    const QRect graph_rect(padding, graph_y, graph_width, graph_height);

    // Draw graph background
    painter.fillRect(graph_rect, graph_background_color);

    // Calculate graph bounds
    const double min_val = std::max(0.0, min_frame_time - 1.0);
    const double max_val = std::max(16.67, max_frame_time + 1.0); // 16.67ms = 60 FPS
    const double range = max_val - min_val;

    if (range <= 0.0) {
        return;
    }

    // Draw grid lines
    painter.setPen(QPen(QColor(80, 80, 80, 100), 1));
    const int grid_lines = 4;
    for (int i = 1; i < grid_lines; ++i) {
        const int y = graph_y + (graph_height * i) / grid_lines;
        painter.drawLine(graph_rect.left(), y, graph_rect.right(), y);
    }

    // Draw 60 FPS line (16.67ms)
    const int fps60_y = graph_y + graph_height - static_cast<int>((16.67 - min_val) / range * graph_height);
    painter.setPen(QPen(QColor(255, 255, 255, 80), 1, Qt::DashLine));
    painter.drawLine(graph_rect.left(), fps60_y, graph_rect.right(), fps60_y);

    // Draw frame time line
    painter.setPen(QPen(graph_line_color, 2));
    painter.setBrush(graph_fill_color);

    QPainterPath graph_path;
    const int point_count = static_cast<int>(frame_times.size());
    const double x_step = static_cast<double>(graph_width) / (point_count - 1);

    for (int i = 0; i < point_count; ++i) {
        const double frame_time = frame_times[i];
        const double normalized_y = (frame_time - min_val) / range;
        const int x = graph_rect.left() + static_cast<int>(i * x_step);
        const int y = graph_y + graph_height - static_cast<int>(normalized_y * graph_height);

        if (i == 0) {
            graph_path.moveTo(x, y);
        } else {
            graph_path.lineTo(x, y);
        }
    }

    // Close the path for filling
    graph_path.lineTo(graph_rect.right(), graph_rect.bottom());
    graph_path.lineTo(graph_rect.left(), graph_rect.bottom());
    graph_path.closeSubpath();

    painter.drawPath(graph_path);

    // Draw statistics text
    painter.setFont(small_font);
    painter.setPen(text_color);

    const QString min_text = QString::fromUtf8("Min: %1ms").arg(FormatFrameTime(min_frame_time));
    const QString avg_text = QString::fromUtf8("Avg: %1ms").arg(FormatFrameTime(avg_frame_time));
    const QString max_text = QString::fromUtf8("Max: %1ms").arg(FormatFrameTime(max_frame_time));

    painter.drawText(graph_rect.left(), graph_y - 5, min_text);
    painter.drawText(graph_rect.center().x() - painter.fontMetrics().horizontalAdvance(avg_text) / 2,
                     graph_y - 5, avg_text);
    painter.drawText(graph_rect.right() - painter.fontMetrics().horizontalAdvance(max_text),
                     graph_y - 5, max_text);
}

void PerformanceOverlay::AddFrameTime(double frame_time_ms) {
    frame_times.push_back(frame_time_ms);

    // Keep only the last MAX_FRAME_HISTORY frames
    if (frame_times.size() > MAX_FRAME_HISTORY) {
        frame_times.pop_front();
    }

    // Update statistics
    if (!frame_times.empty()) {
        min_frame_time = *std::min_element(frame_times.begin(), frame_times.end());
        max_frame_time = *std::max_element(frame_times.begin(), frame_times.end());
        avg_frame_time = std::accumulate(frame_times.begin(), frame_times.end(), 0.0) / frame_times.size();
    }
}

QColor PerformanceOverlay::GetFpsColor(double fps) const {
    if (fps >= 55.0) {
        return QColor(76, 175, 80, 255);    // Material Design green - Good performance
    } else if (fps >= 45.0) {
        return QColor(255, 152, 0, 255);    // Material Design orange - Moderate performance
    } else if (fps >= 30.0) {
        return QColor(255, 87, 34, 255);    // Material Design deep orange - Poor performance
    } else {
        return QColor(244, 67, 54, 255);    // Material Design red - Very poor performance
    }
}

QString PerformanceOverlay::FormatFps(double fps) const {
    if (std::isnan(fps) || fps < 0.0) {
        return QString::fromUtf8("0.0");
    }
    return QString::number(fps, 'f', 1);
}

QString PerformanceOverlay::FormatFrameTime(double frame_time_ms) const {
    if (std::isnan(frame_time_ms) || frame_time_ms < 0.0) {
        return QString::fromUtf8("0.00");
    }
    return QString::number(frame_time_ms, 'f', 2);
}