// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace Loader {
class AppLoader;
}
namespace Ui {
class LoadingScreen;
}
namespace VideoCore {
enum class LoadCallbackStage;
}

class LoadingScreen : public QWidget {
    Q_OBJECT

public:
    explicit LoadingScreen(QWidget* parent = nullptr);
    ~LoadingScreen();

    void Prepare(Loader::AppLoader& loader);
    void Clear();
    void OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value, std::size_t total);
    void OnLoadComplete();
    void paintEvent(QPaintEvent* event) override;

signals:
    void LoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value, std::size_t total);
    void Hidden();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void UpdateLoadingText();

private:
    std::unique_ptr<Ui::LoadingScreen> ui;
    std::size_t previous_total = 0;
    VideoCore::LoadCallbackStage previous_stage;

    QGraphicsOpacityEffect* opacity_effect = nullptr;
    QPropertyAnimation* fadeout_animation = nullptr;
    QTimer* loading_text_animation_timer = nullptr;

    std::unordered_map<VideoCore::LoadCallbackStage, QString> stage_translations;
    QString base_loading_text;

    bool slow_shader_compile_start = false;
    std::chrono::steady_clock::time_point slow_shader_start;
    std::chrono::steady_clock::time_point previous_time;
    std::size_t slow_shader_first_value = 0;
};

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
Q_DECLARE_METATYPE(VideoCore::LoadCallbackStage);
#endif
