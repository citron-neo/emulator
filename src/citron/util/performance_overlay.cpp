// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSizeGrip>
#include <QGridLayout>
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
#pragma comment(lib, "wbemuuid.lib")
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

PerformanceOverlay::PerformanceOverlay(QWidget* parent) : QWidget(UISettings::IsGamescope() ? nullptr : parent) {
    if (parent) {
        main_window = qobject_cast<GMainWindow*>(parent);
    }

    if (UISettings::IsGamescope()) {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
    } else {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    }

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_WState_ExplicitShowHide);

    if (UISettings::IsGamescope()) {
        title_font = QFont(QString::fromUtf8("Segoe UI"), 9, QFont::Bold);
        value_font = QFont(QString::fromUtf8("Segoe UI"), 10, QFont::Bold);
        small_font = QFont(QString::fromUtf8("Segoe UI"), 8, QFont::Normal);
        setMinimumSize(160, 130);
        resize(195, 160);
    } else {
        title_font = QFont(QString::fromUtf8("Segoe UI"), 9, QFont::Medium);
        value_font = QFont(QString::fromUtf8("Segoe UI"), 11, QFont::Bold);
        small_font = QFont(QString::fromUtf8("Segoe UI"), 8, QFont::Normal);
        setMinimumSize(220, 180);
        resize(220, 180);
    }

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    size_grip = new QSizeGrip(this);
    layout->addWidget(size_grip, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    temperature_color = QColor(76, 175, 80, 255);
    graph_background_color = QColor(40, 40, 40, 100);
    graph_line_color = QColor(76, 175, 80, 200);
    graph_fill_color = QColor(76, 175, 80, 60);

    update_timer.setSingleShot(false);
    connect(&update_timer, &QTimer::timeout, this, &PerformanceOverlay::UpdatePerformanceStats);

    if (main_window) {
        connect(main_window, &GMainWindow::themeChanged, this, &PerformanceOverlay::UpdateTheme);
    }

    UpdateTheme();
    UpdatePosition();
}

PerformanceOverlay::~PerformanceOverlay() = default;

void PerformanceOverlay::SetVisible(bool visible) {
    is_enabled = visible;
    is_visible = visible; // Update the state so the check works next time

    if (visible) {
        show();
        update_timer.start(500);
    } else {
        update_timer.stop(); // Stop the timer first
        hide();
    }
}

void PerformanceOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QPainterPath background_path;
    background_path.addRoundedRect(rect(), corner_radius, corner_radius);

    if (!UISettings::IsGamescope()) {
        QPainterPath shadow_path = background_path.translated(1, 1);
        painter.fillPath(shadow_path, QColor(0, 0, 0, 40));
    }

    painter.fillPath(background_path, background_color);
    painter.setPen(QPen(border_color, border_width));
    painter.drawPath(background_path);

    DrawPerformanceInfo(painter);
    DrawFrameGraph(painter);
}

void PerformanceOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdatePosition();
}

void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !size_grip->geometry().contains(event->pos())) {
#if defined(Q_OS_LINUX)
        if (!UISettings::IsGamescope() && windowHandle()) {
            windowHandle()->startSystemMove();
        } else {
            is_dragging = true;
            drag_start_pos = event->globalPosition().toPoint() - this->pos();
        }
#else
        is_dragging = true;
        drag_start_pos = event->globalPosition().toPoint() - this->pos();
#endif
        event->accept();
    }
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (is_dragging) {
        move(event->globalPosition().toPoint() - drag_start_pos);
        event->accept();
    }
}

void PerformanceOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = false;
        has_been_moved = true;
        setCursor(Qt::ArrowCursor);
        event->accept();
    }
    QWidget::mouseReleaseEvent(event);
}

void PerformanceOverlay::UpdatePerformanceStats() {
    if (!main_window || !is_enabled) return;

    if (UISettings::IsGamescope()) {
        bool ui_active = (QApplication::activePopupWidget() != nullptr);

        if (!ui_active) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w->isVisible() && w != main_window && w != this &&
                    !w->inherits("GRenderWindow") &&
                    !w->inherits("VramOverlay") &&
                    !w->inherits("ControllerOverlay") &&
                    !w->inherits("PerformanceOverlay")) {
                    ui_active = true;
                break;
                    }
            }
        }

        if (ui_active) {
            if (!this->isHidden()) this->hide();
            return;
        }

        if (this->isHidden()) {
            this->show();
        }
    } else {
        // Desktop: Only force a show if the user actually has it enabled in the menu
        if (is_enabled && this->isHidden()) {
            this->show();
        }
    }

    shaders_building = main_window->GetShadersBuilding();

    static int update_counter = 0;
    update_counter++;

    if (update_counter % 2 == 0) {
        try {
            current_fps = main_window->GetCurrentFPS();
            current_frame_time = main_window->GetCurrentFrameTime();
            emulation_speed = main_window->GetEmulationSpeed();

            if (std::isnan(current_fps) || current_fps < 0.0 || current_fps > 1000.0) current_fps = 60.0;
            if (std::isnan(current_frame_time) || current_frame_time < 0.0 || current_frame_time > 100.0) current_frame_time = 16.67;
            if (std::isnan(emulation_speed) || emulation_speed < 0.0 || emulation_speed > 1000.0) emulation_speed = 100.0;

            if (current_fps > 0.0) current_frame_time = 1000.0 / current_fps;
        } catch (...) {}
    }

    if (update_counter % 4 == 0) {
        UpdateHardwareTemperatures();
    }

    if (std::isnan(current_fps) || current_fps <= 0.0) current_fps = 60.0;
    if (std::isnan(current_frame_time) || current_frame_time <= 0.0) current_frame_time = 16.67;
    if (std::isnan(emulation_speed) || emulation_speed <= 0.0) emulation_speed = 100.0;

    if (current_frame_time > 0.0) AddFrameTime(current_frame_time);

    fps_color = GetFpsColor(current_fps);
    temperature_color = GetTemperatureColor(std::max({cpu_temperature, gpu_temperature, battery_temperature}));

    update();
}

void PerformanceOverlay::UpdateHardwareTemperatures() {
    cpu_temperature = 0.0f;
    gpu_temperature = 0.0f;
    cpu_sensor_type.clear();
    gpu_sensor_type.clear();
    battery_percentage = 0;
    battery_temperature = 0.0f;

#if defined(Q_OS_LINUX)
    // 1. Read Battery Data (Steam Deck / Laptops)
    QDir bat_dir(QStringLiteral("/sys/class/power_supply/"));
    QStringList bats = bat_dir.entryList({QStringLiteral("BAT*")}, QDir::Dirs);
    for (const QString& node : bats) {
        QFile cap_file(bat_dir.filePath(node + QStringLiteral("/capacity")));
        if (cap_file.open(QIODevice::ReadOnly)) {
            battery_percentage = cap_file.readAll().trimmed().toInt();
            cap_file.close();

            QFile btemp_file(bat_dir.filePath(node + QStringLiteral("/temp")));
            if (btemp_file.open(QIODevice::ReadOnly)) {
                float raw_temp = btemp_file.readAll().trimmed().toFloat();
                // Detect millidegrees (35000) or tenths (350)
                battery_temperature = (raw_temp > 1000) ? raw_temp / 1000.0f : raw_temp / 10.0f;
                btemp_file.close();
            }
            break;
        }
    }

    // 2. Read APU/CPU Temperatures
    QDir hwmon_dir(QStringLiteral("/sys/class/hwmon/"));
    QStringList hwmons = hwmon_dir.entryList({QStringLiteral("hwmon*")}, QDir::Dirs);
    for (const QString& h_node : hwmons) {
        QFile name_file(hwmon_dir.filePath(h_node + QStringLiteral("/name")));
        if (!name_file.open(QIODevice::ReadOnly)) continue;
        QString hw_name = QString::fromUtf8(name_file.readAll().trimmed());
        name_file.close();

        // GPU Portion (Standard Steam Deck & Desktop AMD)
        if (hw_name == QStringLiteral("amdgpu")) {
            QFile t_file(hwmon_dir.filePath(h_node + QStringLiteral("/temp1_input")));
            if (t_file.open(QIODevice::ReadOnly)) {
                gpu_temperature = t_file.readAll().trimmed().toFloat() / 1000.0f;
                gpu_sensor_type = QStringLiteral("GPU");
                t_file.close();
            }
        }
        // CPU Portion (k10temp = AMD Deck/Desktop, coretemp = Intel Desktop)
        else if (hw_name == QStringLiteral("k10temp") || hw_name == QStringLiteral("coretemp") || hw_name == QStringLiteral("zenpower")) {
            // Check for temp1_input (AMD) or temp2_input (Intel coretemp usually starts at 2 for package)
            QStringList input_candidates = {QStringLiteral("temp1_input"), QStringLiteral("temp2_input")};
            for (const auto& input : input_candidates) {
                QFile t_file(hwmon_dir.filePath(h_node + QStringLiteral("/") + input));
                if (t_file.open(QIODevice::ReadOnly)) {
                    cpu_temperature = t_file.readAll().trimmed().toFloat() / 1000.0f;
                    cpu_sensor_type = QStringLiteral("CPU");
                    t_file.close();
                    if (cpu_temperature > 0) break;
                }
            }
        }
    }

    // 3. Fallback to generic thermal_zones
    if (cpu_temperature <= 0.0f) {
        QDir thermal_dir(QStringLiteral("/sys/class/thermal/"));
        QStringList thermal_zones = thermal_dir.entryList({QStringLiteral("thermal_zone*")}, QDir::Dirs);
        for (const QString& zone_name : thermal_zones) {
            QFile temp_file(thermal_dir.filePath(zone_name + QStringLiteral("/temp")));
            if (temp_file.open(QIODevice::ReadOnly)) {
                cpu_temperature = temp_file.readAll().trimmed().toFloat() / 1000.0f;
                cpu_sensor_type = QStringLiteral("CPU");
                temp_file.close();
                if (cpu_temperature > 0) break;
            }
        }
    }
#endif

#if defined(Q_OS_ANDROID)
    QJniObject battery_status = QJniObject::callStaticObjectMethod(
        "android/content/Context", "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        nullptr, new QJniObject("android.content.IntentFilter", "(Ljava/lang/String;)V", "android.intent.action.BATTERY_CHANGED"));

    if (battery_status.isValid()) {
        int level = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                    QJniObject::fromString("level").object<jstring>(), -1);
        int scale = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                    QJniObject::fromString("scale").object<jstring>(), -1);
        int temp_tenths = battery_status.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                                          QJniObject::fromString("temperature").object<jstring>(), -1);

        if (scale > 0) battery_percentage = (level * 100) / scale;
        if (temp_tenths > 0) battery_temperature = static_cast<float>(temp_tenths) / 10.0f;
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
                        cpu_sensor_type = QStringLiteral("CPU");
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
    if (main_window && !has_been_moved) {
        QPoint main_window_pos = main_window->mapToGlobal(QPoint(0, 0));
        move(main_window_pos.x() + 10, main_window_pos.y() + 10);
    }
}

void PerformanceOverlay::DrawPerformanceInfo(QPainter& painter) {
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Dynamic spacing based on font size to prevent squishing
    const int title_step = painter.fontMetrics().height() + 2;
    const int stat_step = painter.fontMetrics().height() + 2;

    int y_left = (padding / 2) + painter.fontMetrics().ascent();
    int y_right = y_left + 10;

    // 1. Draw Title (Left)
    painter.setFont(title_font);
    painter.setPen(text_color);
    painter.drawText(padding, y_left, QStringLiteral("CITRON PERFORMANCE"));

    // 2. Draw Hardware Stats (Right Column)
    painter.setFont(small_font);
    const int hw_step = UISettings::IsGamescope() ? 16 : 20;

    if (cpu_temperature > 0.0f) {
        QString cpu_text = QStringLiteral("CPU:%1°C").arg(cpu_temperature, 0, 'f', 0);
        painter.setPen(GetTemperatureColor(cpu_temperature));
        int tw = painter.fontMetrics().horizontalAdvance(cpu_text);
        painter.drawText(width() - padding - tw, y_right, cpu_text);
        y_right += hw_step;
    }

    if (gpu_temperature > 0.0f) {
        QString gpu_text = QStringLiteral("GPU:%1°C").arg(gpu_temperature, 0, 'f', 0);
        painter.setPen(GetTemperatureColor(gpu_temperature));
        int tw = painter.fontMetrics().horizontalAdvance(gpu_text);
        painter.drawText(width() - padding - tw, y_right, gpu_text);
        y_right += hw_step;
    }

    if (battery_percentage > 0) {
        QString batt_text = QStringLiteral("Battery %:%1%").arg(battery_percentage);
        if (battery_temperature > 0.0f) {
            batt_text += QStringLiteral(" (%1°C)").arg(battery_temperature, 0, 'f', 0);
        }
        painter.setPen(text_color);
        int tw = painter.fontMetrics().horizontalAdvance(batt_text);
        painter.drawText(width() - padding - tw, y_right, batt_text);
    }

    // 3. Draw FPS (Left Column)
    y_left += title_step;
    painter.setFont(value_font);
    painter.setPen(fps_color);
    painter.drawText(padding, y_left, QStringLiteral("%1 FPS").arg(FormatFps(current_fps)));

    // 4. Draw Small Stats (Left Column)
    y_left += title_step;
    painter.setFont(small_font);
    painter.setPen(text_color);
    painter.drawText(padding, y_left, QStringLiteral("Frame:%1 ms").arg(FormatFrameTime(current_frame_time)));

    y_left += stat_step;
    painter.drawText(padding, y_left, QStringLiteral("Speed:%1%").arg(emulation_speed, 0, 'f', 0));

    if (shaders_building > 0) {
        y_left += stat_step;
        painter.setPen(QColor(255, 152, 0));
        painter.drawText(padding, y_left, QStringLiteral("Building:%1").arg(shaders_building));
    }
}

void PerformanceOverlay::DrawFrameGraph(QPainter& painter) {
    if (frame_times.empty()) return;

    const int graph_y = height() - graph_height - padding;
    const int graph_width = width() - (padding * 2);
    const QRect graph_rect(padding, graph_y, graph_width, graph_height);

    painter.fillRect(graph_rect, graph_background_color);

    const double min_val = std::max(0.0, min_frame_time - 1.0);
    const double max_val = std::max(16.67, max_frame_time + 1.0);
    const double range = max_val - min_val;
    if (range <= 0.0) return;

    // Grid lines
    painter.setPen(QPen(QColor(80, 80, 80, 100), 1));
    const int grid_lines = 4;
    for (int i = 1; i < grid_lines; ++i) {
        const int y = graph_y + (graph_height * i) / grid_lines;
        painter.drawLine(graph_rect.left(), y, graph_rect.right(), y);
    }

    // 60 FPS Target line
    const int fps60_y = graph_y + graph_height - static_cast<int>((16.67 - min_val) / range * graph_height);
    painter.setPen(QPen(QColor(255, 255, 255, 80), 1, Qt::DashLine));
    painter.drawLine(graph_rect.left(), fps60_y, graph_rect.right(), fps60_y);

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
        if (i == 0) graph_path.moveTo(x, y); else graph_path.lineTo(x, y);
    }

    graph_path.lineTo(graph_rect.right(), graph_rect.bottom());
    graph_path.lineTo(graph_rect.left(), graph_rect.bottom());
    graph_path.closeSubpath();
    painter.drawPath(graph_path);

    painter.setFont(small_font);
    painter.setPen(text_color);

    const QString min_str = QStringLiteral("Min:%1ms").arg(FormatFrameTime(min_frame_time));
    const QString avg_str = QStringLiteral("Avg:%2ms").arg(FormatFrameTime(avg_frame_time));
    const QString max_str = QStringLiteral("Max:%1ms").arg(FormatFrameTime(max_frame_time));

    // Combine into one line for measurement
    const QString full_line = QStringLiteral("%1  %2  %3").arg(min_str, avg_str, max_str);
    int total_width = painter.fontMetrics().horizontalAdvance(full_line);

    // If there is enough room, flatten it across the top. Otherwise, stack it.
    if (total_width < graph_width - 10) {
        // Flat layout
        painter.drawText(graph_rect.left(), graph_y - 6, full_line);
    } else {
        // Stacked layout (Fallback for small windows/High-DPI scaling)
        painter.drawText(graph_rect.left(), graph_y - 18, QStringLiteral("%1 %2").arg(min_str, avg_str));
        painter.drawText(graph_rect.left(), graph_y - 4, max_str);
    }
}

void PerformanceOverlay::AddFrameTime(double frame_time_ms) {
    frame_times.push_back(frame_time_ms);
    if (frame_times.size() > MAX_FRAME_HISTORY) frame_times.pop_front();
    if (!frame_times.empty()) {
        min_frame_time = *std::min_element(frame_times.begin(), frame_times.end());
        max_frame_time = *std::max_element(frame_times.begin(), frame_times.end());
        avg_frame_time = std::accumulate(frame_times.begin(), frame_times.end(), 0.0) / frame_times.size();
    }
}

QColor PerformanceOverlay::GetFpsColor(double fps) const {
    if (fps >= 55.0) return QColor(76, 175, 80, 255);
    if (fps >= 45.0) return QColor(255, 152, 0, 255);
    if (fps >= 30.0) return QColor(255, 87, 34, 255);
    return QColor(244, 67, 54, 255);
}

QColor PerformanceOverlay::GetTemperatureColor(float temperature) const {
    if (temperature > 85.0f) return QColor(244, 67, 54, 255);
    if (temperature > 75.0f) return QColor(255, 152, 0, 255);
    return QColor(76, 175, 80, 255);
}

QString PerformanceOverlay::FormatFps(double fps) const {
    if (std::isnan(fps) || fps < 0.0) return QString::fromUtf8("0.0");
    return QString::number(fps, 'f', 1);
}

QString PerformanceOverlay::FormatFrameTime(double frame_time_ms) const {
    if (std::isnan(frame_time_ms) || frame_time_ms < 0.0) return QString::fromUtf8("0.00");
    return QString::number(frame_time_ms, 'f', 2);
}

void PerformanceOverlay::UpdateTheme() {
    if (UISettings::IsDarkTheme()) {
        background_color = QColor(20, 20, 20, 200);
        border_color = QColor(60, 60, 60, 120);
        text_color = QColor(220, 220, 220, 255);
        graph_background_color = QColor(40, 40, 40, 100);
    } else {
        background_color = QColor(245, 245, 245, 220);
        border_color = QColor(200, 200, 200, 120);
        text_color = QColor(20, 20, 20, 255);
        graph_background_color = QColor(220, 220, 220, 100);
    }
    update();
}
