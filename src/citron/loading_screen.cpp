// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/loading_screen.h"
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyleOption>
#include <QTime>
#include "citron/theme.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    opacity_effect = new QGraphicsOpacityEffect(ui->fade_parent);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = new QPropertyAnimation(opacity_effect, "opacity", this);
    fadeout_animation->setDuration(400);
    fadeout_animation->setEasingCurve(QEasingCurve::OutQuad);
    fadeout_animation->setStartValue(1.0);
    fadeout_animation->setEndValue(0.0);

    connect(fadeout_animation, &QPropertyAnimation::finished, this, [this] {
        hide();
        opacity_effect->setOpacity(1.0);
        emit Hidden();
    });

    loading_text_animation_timer = new QTimer(this);
    connect(loading_text_animation_timer, &QTimer::timeout, this, &LoadingScreen::UpdateLoadingText);

    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);
    qRegisterMetaType<VideoCore::LoadCallbackStage>();
}

LoadingScreen::~LoadingScreen() {
    loading_text_animation_timer->stop();
}

void LoadingScreen::Prepare(Loader::AppLoader& loader) {
    QPixmap game_icon_pixmap;
    std::vector<u8> buffer;
    if (loader.ReadIcon(buffer) == Loader::ResultStatus::Success) {
        game_icon_pixmap.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
    } else {
        game_icon_pixmap = QPixmap(QStringLiteral(":/icons/scalable/actions/games.svg"));
    }

    if (!game_icon_pixmap.isNull()) {
        QPixmap rounded_pixmap(game_icon_pixmap.size());
        rounded_pixmap.fill(Qt::transparent);
        QPainter painter(&rounded_pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const int radius = game_icon_pixmap.width() / 6;
        path.addRoundedRect(rounded_pixmap.rect(), radius, radius);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, game_icon_pixmap);
        ui->game_icon->setPixmap(rounded_pixmap.scaled(ui->game_icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        ui->game_icon->setPixmap(game_icon_pixmap);
    }

    std::string title;
    if (loader.ReadTitle(title) == Loader::ResultStatus::Success && !title.empty()) {
        stage_translations = {
            {VideoCore::LoadCallbackStage::Prepare, tr("Loading %1").arg(QString::fromStdString(title))},
            {VideoCore::LoadCallbackStage::Build, tr("Loading %1").arg(QString::fromStdString(title))},
            {VideoCore::LoadCallbackStage::Complete, tr("Launching...")},
        };
    } else {
         stage_translations = {
            {VideoCore::LoadCallbackStage::Prepare, tr("Loading Game...")},
            {VideoCore::LoadCallbackStage::Build, tr("Loading Game...")},
            {VideoCore::LoadCallbackStage::Complete, tr("Launching...")},
        };
    }

    slow_shader_compile_start = false;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::showEvent(QShowEvent* event) {
    opacity_effect->setOpacity(0.0);
    auto fade_in = new QPropertyAnimation(opacity_effect, "opacity", this);
    fade_in->setDuration(400);
    fade_in->setStartValue(0.0);
    fade_in->setEndValue(1.0);
    fade_in->setEasingCurve(QEasingCurve::OutQuad);
    fade_in->start(QAbstractAnimation::DeleteWhenStopped);

    QWidget::showEvent(event);
}

void LoadingScreen::UpdateLoadingText() {
    int dot_count = ui->stage->text().count(QLatin1Char('.'));
    QString new_text = base_loading_text;
    if (dot_count >= 3) {
        // Cycle back to no dots
    } else {
        new_text.append(QString(dot_count + 1, QLatin1Char('.')));
    }
    ui->stage->setText(new_text);
}

void LoadingScreen::OnLoadComplete() {
    loading_text_animation_timer->stop();
    fadeout_animation->start();
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    using namespace std::chrono;
    const auto now = steady_clock::now();

    if (stage != previous_stage) {
        QString style;
        switch (stage) {
        case VideoCore::LoadCallbackStage::Build:
            style = QString::fromUtf8(R"(
                QProgressBar { background-color: #3a3a3a; border: none; border-radius: 4px; }
                QProgressBar::chunk { background-color: %1; border-radius: 4px; }
            )").arg(Theme::GetAccentColor());
            break;
        case VideoCore::LoadCallbackStage::Complete:
            style = QString::fromUtf8(R"(
                QProgressBar { background-color: #3a3a3a; border: none; border-radius: 4px; }
                QProgressBar::chunk { background-color: %1; border-radius: 4px; }
            )").arg(Theme::GetAccentColor());
            break;
        default:
            style = QStringLiteral("");
            break;
        }
        ui->shader_progress_bar->setStyleSheet(style);
        ui->progress_bar->setStyleSheet(style);

        base_loading_text = stage_translations[stage];
        const QFontMetrics metrics(ui->stage->font());
        const int max_width = metrics.horizontalAdvance(base_loading_text + QLatin1String("..."));
        ui->stage->setFixedWidth(max_width);
        ui->stage->setText(base_loading_text);

        if (stage == VideoCore::LoadCallbackStage::Complete) {
            loading_text_animation_timer->stop();
        } else {
            loading_text_animation_timer->start(500);
        }

        ui->progress_bar->setVisible(stage == VideoCore::LoadCallbackStage::Complete);
        ui->shader_widget->setVisible(stage == VideoCore::LoadCallbackStage::Build);

        previous_stage = stage;
        slow_shader_compile_start = false;
    }

    if (stage == VideoCore::LoadCallbackStage::Complete) {
        ui->progress_bar->setRange(0, 0);
    }

    if (stage == VideoCore::LoadCallbackStage::Build) {
        if (total != previous_total) {
            ui->shader_progress_bar->setMaximum(static_cast<int>(total));
            previous_total = total;
        }
        ui->shader_progress_bar->setValue(static_cast<int>(value));

        QString estimate;
        if (now - previous_time > milliseconds{50} || slow_shader_compile_start) {
            if (!slow_shader_compile_start) {
                slow_shader_start = steady_clock::now();
                slow_shader_compile_start = true;
                slow_shader_first_value = value;
            }
            const auto diff = duration_cast<milliseconds>(now - slow_shader_start);
            if (diff > seconds{1} && (value - slow_shader_first_value > 0)) {
                const auto eta_mseconds =
                    static_cast<long>(static_cast<double>(total - slow_shader_first_value) /
                                      (value - slow_shader_first_value) * diff.count());
                estimate =
                    tr("ETA: %1")
                        .arg(QTime(0, 0, 0, 0)
                                 .addMSecs(std::max<long>(eta_mseconds - diff.count(), 0))
                                 .toString(QStringLiteral("mm:ss")));
            }
        }

        ui->shader_stage_label->setText(tr("Building Shaders..."));

        if (!estimate.isEmpty()) {
            ui->shader_value_label->setText(QStringLiteral("%1 / %2 (%3)").arg(value).arg(total).arg(estimate));
        } else {
            ui->shader_value_label->setText(QStringLiteral("%1 / %2").arg(value).arg(total));
        }
    }
    previous_time = now;
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void LoadingScreen::Clear() {
    ui->game_icon->clear();
    loading_text_animation_timer->stop();
}
