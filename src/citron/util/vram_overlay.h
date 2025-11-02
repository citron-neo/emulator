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

struct VramUsageData {
    u64 total_vram = 0;
    u64 used_vram = 0;
    u64 buffer_memory = 0;
    u64 texture_memory = 0;
    u64 staging_memory = 0;
    u64 available_vram = 0;
    bool leak_detected = false;
    u64 leak_increase_mb = 0;
    u32 cleanup_count = 0;
    double vram_percentage = 0.0;
};

class VramOverlay : public QWidget {
    Q_OBJECT

public:
    explicit VramOverlay(GMainWindow* parent);
    ~VramOverlay() override;

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
    void UpdateVramStats();

private:
    void UpdatePosition();
    void DrawVramInfo(QPainter& painter);
    void DrawVramGraph(QPainter& painter);
    void DrawLeakWarning(QPainter& painter);
    QColor GetVramColor(double percentage) const;
    QString FormatMemorySize(u64 bytes) const;
    QString FormatPercentage(double percentage) const;
    void AddVramUsage(double percentage);

    GMainWindow* main_window;
    QTimer update_timer;

    // VRAM data
    VramUsageData current_vram_data;
    u64 last_vram_usage = 0;
    u32 frame_counter = 0;

    // VRAM graph data
    static constexpr size_t MAX_VRAM_HISTORY = 120; // 2 seconds at 60 FPS
    std::deque<double> vram_usage_history;
    double min_vram_usage = 0.0;
    double max_vram_usage = 100.0;

    // Display settings
    bool is_visible = false;
    bool is_dragging = false;
    bool has_been_moved = false;
    QPoint drag_start_pos;
    QPoint widget_start_pos;
    QFont title_font;
    QFont value_font;
    QFont small_font;
    QFont warning_font;

    // Colors
    QColor background_color;
    QColor border_color;
    QColor text_color;
    QColor secondary_text_color;
    QColor vram_safe_color;      // Green for < 70%
    QColor vram_warning_color;   // Yellow for 70-90%
    QColor vram_danger_color;    // Red for > 90%
    QColor leak_warning_color;   // Orange for leak detection
    QColor graph_background_color;
    QColor graph_grid_color;
    QColor graph_line_color;
    QColor graph_fill_color;

    // Layout constants
    static constexpr int padding = 12;
    static constexpr int corner_radius = 8;
    static constexpr int border_width = 1;
    static constexpr int graph_height = 60;
    static constexpr int warning_height = 30;
};
