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

#include <QtGlobal>
#include <QWindow>

#include "citron/main.h"
#include "citron/util/vram_overlay.h"
#include "citron/uisettings.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "common/settings.h"

VramOverlay::VramOverlay(GMainWindow* parent)
: QWidget(parent), main_window(parent) {

    // Set up the widget properties
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);

    // Initialize fonts with clean typography
    title_font = QFont(QString::fromUtf8("Segoe UI"), 11, QFont::Bold);
    value_font = QFont(QString::fromUtf8("Segoe UI"), 10, QFont::Medium);
    small_font = QFont(QString::fromUtf8("Segoe UI"), 9, QFont::Normal);
    warning_font = QFont(QString::fromUtf8("Segoe UI"), 10, QFont::Bold);

    // VRAM usage colors - modern palette
    vram_safe_color = QColor(76, 175, 80, 255);
    vram_warning_color = QColor(255, 193, 7, 255);
    vram_danger_color = QColor(244, 67, 54, 255);
    leak_warning_color = QColor(255, 152, 0, 255);

    // Graph colors - clean and modern
    graph_background_color = QColor(25, 25, 25, 255);
    graph_grid_color = QColor(60, 60, 60, 100);
    graph_line_color = QColor(76, 175, 80, 255);
    graph_fill_color = QColor(76, 175, 80, 40);

    // Set up timer for updates
    update_timer.setSingleShot(false);
    connect(&update_timer, &QTimer::timeout, this, &VramOverlay::UpdateVramStats);

    connect(parent, &GMainWindow::themeChanged, this, &VramOverlay::UpdateTheme);
    UpdateTheme();

    // Set clean, compact size
    resize(250, 180);

    // Position in top-right corner
    UpdatePosition();
}

VramOverlay::~VramOverlay() = default;

void VramOverlay::SetVisible(bool visible) {
    if (is_visible == visible) {
        return;
    }
    is_visible = visible;
    if (visible) {
        show();
        update_timer.start(1000); // Update every 1 second
    } else {
        hide();
        update_timer.stop();
    }
}

void VramOverlay::UpdatePosition() {
    if (main_window && !has_been_moved) {
        QPoint main_window_pos = main_window->mapToGlobal(QPoint(0,0));
        QSize main_window_size = main_window->size();
        move(main_window_pos.x() + main_window_size.width() - width() - 15, main_window_pos.y() + 15);
    }
}

void VramOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QPainterPath background_path;
    background_path.addRoundedRect(rect(), corner_radius, corner_radius);
    painter.fillPath(background_path, background_color);

    painter.setPen(QPen(border_color, border_width));
    painter.drawPath(background_path);

    DrawVramInfo(painter);
    DrawVramGraph(painter);
    if (current_vram_data.leak_detected) {
        DrawLeakWarning(painter);
    }
}

void VramOverlay::DrawVramInfo(QPainter& painter) {
    const int section_padding = 12;
    const int line_height = 14;
    const int section_spacing = 6;
    int y_offset = section_padding + 4;

    painter.setFont(title_font);
    painter.setPen(text_color);
    painter.drawText(section_padding, y_offset, QString::fromUtf8("VRAM Monitor"));
    y_offset += line_height + section_spacing;

    painter.setFont(value_font);
    QColor vram_color = GetVramColor(current_vram_data.vram_percentage);
    painter.setPen(vram_color);
    QString vram_text = QString::fromUtf8("%1 / %2 (%3%)")
    .arg(FormatMemorySize(current_vram_data.used_vram))
    .arg(FormatMemorySize(current_vram_data.total_vram))
    .arg(FormatPercentage(current_vram_data.vram_percentage));
    painter.drawText(section_padding, y_offset, vram_text);
    y_offset += line_height + section_spacing;

    painter.setFont(small_font);
    painter.setPen(secondary_text_color);
    QString buffer_text = QString::fromUtf8("Buffers: %1").arg(FormatMemorySize(current_vram_data.buffer_memory));
    painter.drawText(section_padding, y_offset, buffer_text);
    y_offset += line_height - 1;
    QString texture_text = QString::fromUtf8("Textures: %1").arg(FormatMemorySize(current_vram_data.texture_memory));
    painter.drawText(section_padding, y_offset, texture_text);
    y_offset += line_height - 1;
    QString staging_text = QString::fromUtf8("Staging: %1").arg(FormatMemorySize(current_vram_data.staging_memory));
    painter.drawText(section_padding, y_offset, staging_text);
    y_offset += line_height + section_spacing;

    painter.setPen(secondary_text_color);
    QString mode_text;
    switch (Settings::values.vram_usage_mode.GetValue()) {
        case Settings::VramUsageMode::Conservative: mode_text = QString::fromUtf8("Mode: Conservative"); break;
        case Settings::VramUsageMode::Aggressive: mode_text = QString::fromUtf8("Mode: Aggressive"); break;
        case Settings::VramUsageMode::HighEnd: mode_text = QString::fromUtf8("Mode: High-End GPU"); break;
        case Settings::VramUsageMode::Insane: mode_text = QString::fromUtf8("Mode: Insane"); painter.setPen(leak_warning_color); break;
        default: mode_text = QString::fromUtf8("Mode: Unknown"); break;
    }
    painter.drawText(section_padding, y_offset, mode_text);
}

void VramOverlay::DrawVramGraph(QPainter& painter) {
    if (vram_usage_history.empty()) return;

    const int graph_padding = 12;
    const int graph_y = height() - 60;
    const int graph_width = width() - (graph_padding * 2);
    const int local_graph_height = 40;

    QRect graph_rect(graph_padding, graph_y, graph_width, local_graph_height);
    QPainterPath graph_path;
    graph_path.addRoundedRect(graph_rect, 3, 3);
    painter.fillPath(graph_path, graph_background_color);

    painter.setPen(QPen(graph_grid_color, 1));
    painter.drawPath(graph_path);

    for (int i = 1; i < 4; ++i) {
        int y = graph_y + (i * local_graph_height / 4);
        painter.drawLine(graph_padding + 1, y, graph_padding + graph_width - 1, y);
    }

    if (vram_usage_history.size() > 1) {
        painter.setPen(QPen(graph_line_color, 2));
        QPainterPath line_path;
        for (size_t i = 0; i < vram_usage_history.size(); ++i) {
            double x = graph_padding + 2 + (static_cast<double>(i) / (vram_usage_history.size() - 1)) * (graph_width - 4);
            double y = graph_y + local_graph_height - 2 - (vram_usage_history[i] / 100.0) * (local_graph_height - 4);
            if (i == 0) line_path.moveTo(x, y); else line_path.lineTo(x, y);
        }
        painter.drawPath(line_path);

        line_path.lineTo(graph_padding + graph_width - 2, graph_y + local_graph_height - 2);
        line_path.lineTo(graph_padding + 2, graph_y + local_graph_height - 2);
        line_path.closeSubpath();
        painter.fillPath(line_path, graph_fill_color);
    }
}

void VramOverlay::DrawLeakWarning(QPainter& painter) {
    const int warning_y = height() - 20;
    QRect warning_rect(padding, warning_y, width() - (padding * 2), 16);
    QPainterPath warning_path;
    warning_path.addRoundedRect(warning_rect, 2, 2);
    painter.fillPath(warning_path, QColor(255, 152, 0, 80));
    painter.setFont(small_font);
    painter.setPen(leak_warning_color);
    QString warning_text = QString::fromUtf8("⚠ Leak: +%1 MB").arg(current_vram_data.leak_increase_mb);
    painter.drawText(warning_rect, Qt::AlignCenter, warning_text);
}

void VramOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdatePosition();
}

#if defined(Q_OS_LINUX)
// LINUX-SPECIFIC IMPLEMENTATION (Wayland Fix)
void VramOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (windowHandle()) {
            windowHandle()->startSystemMove();
        }
    }
    QWidget::mousePressEvent(event);
}

void VramOverlay::mouseMoveEvent(QMouseEvent* event) {
    // Intentionally blank, the system compositor handles the move.
    QWidget::mouseMoveEvent(event);
}

#else
// ORIGINAL IMPLEMENTATION (For Windows, Android, etc.)
void VramOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = true;
        drag_start_pos = event->globalPosition().toPoint();
        widget_start_pos = pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void VramOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (is_dragging) {
        QPoint delta = event->globalPosition().toPoint() - drag_start_pos;
        move(widget_start_pos + delta);
    }
    QWidget::mouseMoveEvent(event);
}
#endif

void VramOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = false;
        has_been_moved = true;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void VramOverlay::UpdateVramStats() {
    if (!main_window) return;
    try {
        current_vram_data.total_vram = main_window->GetTotalVram();
        current_vram_data.used_vram = main_window->GetUsedVram();
        current_vram_data.buffer_memory = main_window->GetBufferMemoryUsage();
        current_vram_data.texture_memory = main_window->GetTextureMemoryUsage();
        current_vram_data.staging_memory = main_window->GetStagingMemoryUsage();

        if (current_vram_data.total_vram > 0) {
            current_vram_data.vram_percentage = (static_cast<double>(current_vram_data.used_vram) / current_vram_data.total_vram) * 100.0;
            current_vram_data.available_vram = current_vram_data.total_vram - current_vram_data.used_vram;
        } else {
            current_vram_data.vram_percentage = 0.0;
            current_vram_data.available_vram = 0;
        }

        frame_counter++;
        if (frame_counter % 10 == 0) {
            if (last_vram_usage > 0 && current_vram_data.used_vram > last_vram_usage + (50 * 1024 * 1024)) {
                current_vram_data.leak_detected = true;
                current_vram_data.leak_increase_mb = (current_vram_data.used_vram - last_vram_usage) / (1024 * 1024);
            } else {
                current_vram_data.leak_detected = false;
                current_vram_data.leak_increase_mb = 0;
            }
            last_vram_usage = current_vram_data.used_vram;
        }
        AddVramUsage(current_vram_data.vram_percentage);
        update();
    } catch (...) {
        // Ignore
    }
}

QColor VramOverlay::GetVramColor(double percentage) const {
    if (percentage < 70.0) return vram_safe_color;
    if (percentage < 90.0) return vram_warning_color;
    return vram_danger_color;
}

QString VramOverlay::FormatMemorySize(u64 bytes) const {
    if (bytes >= 1024 * 1024 * 1024) return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB");
    if (bytes >= 1024 * 1024) return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    if (bytes >= 1024) return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes) + QStringLiteral(" B");
}

QString VramOverlay::FormatPercentage(double percentage) const {
    return QString::number(percentage, 'f', 1);
}

void VramOverlay::AddVramUsage(double percentage) {
    vram_usage_history.push_back(percentage);
    if (vram_usage_history.size() > MAX_VRAM_HISTORY) {
        vram_usage_history.pop_front();
    }
    if (!vram_usage_history.empty()) {
        min_vram_usage = *std::min_element(vram_usage_history.begin(), vram_usage_history.end());
        max_vram_usage = *std::max_element(vram_usage_history.begin(), vram_usage_history.end());
        double range = max_vram_usage - min_vram_usage;
        if (range < 10.0) range = 10.0;
        min_vram_usage = std::max(0.0, min_vram_usage - range * 0.1);
        max_vram_usage = std::min(100.0, max_vram_usage + range * 0.1);
    }
}

void VramOverlay::UpdateTheme() {
    if (UISettings::IsDarkTheme()) {
        // Dark Theme Colors (your original values)
        background_color = QColor(15, 15, 15, 220);
        border_color = QColor(45, 45, 45, 255);
        text_color = QColor(240, 240, 240, 255);
        secondary_text_color = QColor(180, 180, 180, 255);
        graph_background_color = QColor(25, 25, 25, 255);
        graph_grid_color = QColor(60, 60, 60, 100);
    } else {
        // Light Theme Colors
        background_color = QColor(245, 245, 245, 220);
        border_color = QColor(200, 200, 200, 255);
        text_color = QColor(20, 20, 20, 255);
        secondary_text_color = QColor(80, 80, 80, 255);
        graph_background_color = QColor(225, 225, 225, 255);
        graph_grid_color = QColor(190, 190, 190, 100);
    }
    update(); // Force a repaint
}
