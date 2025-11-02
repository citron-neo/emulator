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
#include <QDir>
#include <QFile>
#include <QStringList>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <comdef.h>
#include <WbemIdl.h>
#pragma comment(lib, "wbemuuid.lib") // For MSVC, helps the linker find the library
#endif

#ifdef Q_OS_ANDROID
#include <QtAndroidExtras>
#endif

#include "citron/main.h"
#include "citron/util/performance_overlay.h"
#include "citron/uisettings.h"
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

    temperature_color = QColor(76, 175, 80, 255); // Default to green

    // Graph colors
    graph_background_color = QColor(40, 40, 40, 100);
    graph_line_color = QColor(76, 175, 80, 200);
    graph_fill_color = QColor(76, 175, 80, 60);

    // Set up timer for updates
    update_timer.setSingleShot(false);
    connect(&update_timer, &QTimer::timeout, this, &PerformanceOverlay::UpdatePerformanceStats);

    // Connect to the main window's theme change signal
    connect(parent, &GMainWindow::themeChanged, this, &PerformanceOverlay::UpdateTheme);
    // Set the initial theme colors
    UpdateTheme();

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

#if defined(Q_OS_LINUX)
// LINUX-SPECIFIC IMPLEMENTATION (Wayland Fix)
void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Hand off window moving responsibility to the OS compositor.
        if (windowHandle()) {
            windowHandle()->startSystemMove();
        }
    }
    QWidget::mousePressEvent(event);
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    // This function is intentionally left blank for dragging, as the
    // system compositor now handles the entire move operation.
    QWidget::mouseMoveEvent(event);
}

#else
// ORIGINAL IMPLEMENTATION
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
#endif

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

    // Update hardware temperatures every 4th update (every 2 seconds)
    if (update_counter % 4 == 0) {
        UpdateHardwareTemperatures();
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

    // Update FPS and Temperature colors based on performance
    fps_color = GetFpsColor(current_fps);
    temperature_color = GetTemperatureColor(std::max({cpu_temperature, gpu_temperature, battery_temperature}));

    // Trigger a repaint
    update();
}

void PerformanceOverlay::UpdateHardwareTemperatures() {
    // Reset data
    cpu_temperature = 0.0f;
    gpu_temperature = 0.0f;
    cpu_sensor_type.clear();
    gpu_sensor_type.clear();
    battery_percentage = 0;
    battery_temperature = 0.0f;

#if defined(Q_OS_LINUX)
    // --- Standard Linux Thermal Zone Reading ---
    QDir thermal_dir(QString::fromUtf8("/sys/class/thermal/"));
    QStringList filters{QString::fromUtf8("thermal_zone*")};
    QStringList thermal_zones = thermal_dir.entryList(filters, QDir::Dirs);

    for (const QString& zone_name : thermal_zones) {
        QFile type_file(thermal_dir.filePath(zone_name + QString::fromUtf8("/type")));
        if (!type_file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString type = QString::fromUtf8(type_file.readAll()).trimmed();
        type_file.close();

        QFile temp_file(thermal_dir.filePath(zone_name + QString::fromUtf8("/temp")));
        if (!temp_file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        float temp = temp_file.readAll().trimmed().toFloat() / 1000.0f;
        temp_file.close();

        if (type.contains(QString::fromUtf8("x86_pkg_temp")) || type.contains(QString::fromUtf8("cpu"))) {
            if (temp > cpu_temperature) {
                cpu_temperature = temp;
                cpu_sensor_type = QString::fromUtf8("CPU");
            }
        } else if (type.contains(QString::fromUtf8("radeon")) || type.contains(QString::fromUtf8("amdgpu")) || type.contains(QString::fromUtf8("nvidia")) || type.contains(QString::fromUtf8("nouveau"))) {
            if (temp > gpu_temperature) {
                gpu_temperature = temp;
                gpu_sensor_type = QString::fromUtf8("GPU");
            }
        }
    }
#endif

#if defined(Q_OS_ANDROID)
    // This uses QtAndroid Extras to get battery info from the Android system.
    // NOTE: This requires the QtAndroidExtras module to be linked in the build.
    QJniObject battery_status = QJniObject::callStaticObjectMethod(
        "android/content/CONTEXT", "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        nullptr, new QJniObject("android.content.IntentFilter", "(Ljava/lang/String;)V", "android.intent.action.BATTERY_CHANGED"));

    if (battery_status.isValid()) {
        int level = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                    QJniObject::fromString("level").object<jstring>(), -1);
        int scale = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                    QJniObject::fromString("scale").object<jstring>(), -1);
        int temp_tenths = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                          QJniObject::fromString("temperature").object<jstring>(), -1);

        if (scale > 0) {
            battery_percentage = (level * 100) / scale;
        }
        if (temp_tenths > 0) {
            battery_temperature = static_cast<float>(temp_tenths) / 10.0f;
        }
    }
#endif

#if defined(Q_OS_WIN)
    HRESULT hres;
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;

    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hres)) {
        hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (SUCCEEDED(hres)) {
            hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                     RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            if (SUCCEEDED(hres)) {
                IEnumWbemClassObject* pEnumerator = nullptr;
                hres = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM MSAcpi_ThermalZoneTemperature"),
                                       WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
                if (SUCCEEDED(hres)) {
                    IWbemClassObject* pclsObj = nullptr;
                    ULONG uReturn = 0;
                    while (pEnumerator) {
                        pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                        if (uReturn == 0) break;

                        VARIANT vtProp;
                        pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0);
                        float temp_kelvin = vtProp.uintVal / 10.0f;
                        cpu_temperature = temp_kelvin - 273.15f;
                        cpu_sensor_type = QString::fromUtf8("CPU");
                        VariantClear(&vtProp);
                        pclsObj->Release();
                    }
                    pEnumerator->Release();
                }
            }
        }
    }
    if(pSvc) pSvc->Release();
    if(pLoc) pLoc->Release();
#endif
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

    int y_offset = padding;
    const int line_height = 20;

    // Draw title
    painter.setFont(title_font);
    painter.setPen(text_color);
    painter.drawText(padding, y_offset + 12, QString::fromUtf8("CITRON"));

    int y_offset_right = padding;
    const int line_height_right = 18;

    // Draw Temperatures
    painter.setFont(small_font);

    float core_temp_to_display = std::max(cpu_temperature, gpu_temperature);
    if (core_temp_to_display > 0.0f) {
        QString core_label = gpu_temperature > cpu_temperature ? gpu_sensor_type : cpu_sensor_type;
        QString core_temp_text = QString::fromUtf8("%1: %2°C").arg(core_label).arg(core_temp_to_display, 0, 'f', 0);
        painter.setPen(GetTemperatureColor(core_temp_to_display));
        int text_width = painter.fontMetrics().horizontalAdvance(core_temp_text);
        painter.drawText(width() - padding - text_width, y_offset_right + 12, core_temp_text);
    }
    y_offset_right += line_height_right;

    // Draw Battery info
    if (battery_percentage > 0) {
        QString batt_text = QString::fromUtf8("Batt: %1%").arg(battery_percentage);
        if (battery_temperature > 0.0f) {
            batt_text += QString::fromUtf8(" (%1°C)").arg(battery_temperature, 0, 'f', 0);
        }
        painter.setPen(text_color);
        int text_width = painter.fontMetrics().horizontalAdvance(batt_text);
        painter.drawText(width() - padding - text_width, y_offset_right + 12, batt_text);
    }

    y_offset += line_height + 4;

    // Draw FPS
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
    const double x_step = static_cast<double>(graph_width) / (std::max(1, point_count - 1));

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

QColor PerformanceOverlay::GetTemperatureColor(float temperature) const {
    if (temperature > 70.0f) {
        return QColor(244, 67, 54, 255); // Material Design red
    } else if (temperature > 60.0f) {
        return QColor(255, 152, 0, 255); // Material Design orange
    } else {
        return QColor(76, 175, 80, 255); // Material Design green
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

void PerformanceOverlay::UpdateTheme() {
    if (UISettings::IsDarkTheme()) {
        // Dark Theme Colors (your original values)
        background_color = QColor(20, 20, 20, 200); // Slightly more opaque
        border_color = QColor(60, 60, 60, 120);
        text_color = QColor(220, 220, 220, 255);
        graph_background_color = QColor(40, 40, 40, 100);
    } else {
        // Light Theme Colors
        background_color = QColor(245, 245, 245, 220);
        border_color = QColor(200, 200, 200, 120);
        text_color = QColor(20, 20, 20, 255);
        graph_background_color = QColor(220, 220, 220, 100);
    }
    update(); // Force a repaint with the new colors
}
