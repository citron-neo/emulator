// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "citron/game_list_loading_overlay.h"
#include "citron/spinning_logo.h"

GameListLoadingOverlay::GameListLoadingOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    m_logo = new SpinningLogo(this);
    m_logo->setPixmap(QPixmap(QStringLiteral(":/citron.svg")));
    m_logo->setFixedSize(120, 120);
    m_logo->setSpinMode(SpinningLogo::SpinMode::Spinning);
    
    layout->addStretch();
    layout->addWidget(m_logo, 0, Qt::AlignCenter);
    layout->addSpacing(80); // Space for the capsule
    layout->addStretch();

    // Use Graphics Opacity Effect for the entire widget contents
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    effect->setOpacity(0.0);

    m_current_status = tr("Loading Game List...");
    hide();
}

GameListLoadingOverlay::~GameListLoadingOverlay() = default;

void GameListLoadingOverlay::SetStatusText(const QString& text) {
    m_current_status = text;
    update();
}

void GameListLoadingOverlay::ShowLoading() {
    SetStatusText(tr("Loading Game List..."));
    m_logo->setSpinMode(SpinningLogo::SpinMode::Spinning);
    
    if (graphicsEffect()) {
        static_cast<QGraphicsOpacityEffect*>(graphicsEffect())->setOpacity(1.0);
    }
    
    m_is_fading_out = false;
    show();
    raise();
}

void GameListLoadingOverlay::ShowPopulated() {
    SetStatusText(tr("Game List Populated!"));
    m_logo->setSpinMode(SpinningLogo::SpinMode::None);
    update();
}

void GameListLoadingOverlay::FadeOut() {
    if (m_is_fading_out) return;
    m_is_fading_out = true;

    auto* effect = static_cast<QGraphicsOpacityEffect*>(graphicsEffect());
    if (!effect) {
        hide();
        return;
    }

    auto* anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(500);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
        hide();
        anim->deleteLater();
    });
    anim->start();
}

void GameListLoadingOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    // Full screen semi-transparent dim
    painter.fillRect(rect(), QColor(0, 0, 0, 140));

    // ---- Premium Loading Text Capsule ----
    QFont font = this->font();
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    int text_w = painter.fontMetrics().horizontalAdvance(m_current_status);
    int text_h = painter.fontMetrics().height();
    
    // Capsule dimensions (anchored below logo)
    int pad_h = 30, pad_v = 12;
    int capsule_w = text_w + (pad_h * 2);
    int capsule_h = text_h + (pad_v * 2);
    
    QRect logo_rect = m_logo->geometry();
    QRect capsule_rect(logo_rect.center().x() - (capsule_w / 2),
                      logo_rect.bottom() + 40,
                      capsule_w,
                      capsule_h);
                      
    // Draw Capsule (Glassmorphism / Onyx Shadow)
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1.2));
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(capsule_rect, capsule_h / 2, capsule_h / 2);
    
    // Subtle inner glow
    painter.setPen(QPen(QColor(255, 255, 255, 15), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(capsule_rect.adjusted(1,1,-1,-1), capsule_h / 2, capsule_h / 2);
    
    // Draw Text with subtle drop shadow
    painter.setPen(QColor(0, 0, 0, 150));
    painter.drawText(capsule_rect.translated(1, 1), Qt::AlignCenter, m_current_status);
    
    painter.setPen(Qt::white);
    painter.drawText(capsule_rect, Qt::AlignCenter, m_current_status);
}

void GameListLoadingOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateLayout();
}

void GameListLoadingOverlay::UpdateLayout() {
    if (parentWidget()) {
        resize(parentWidget()->size());
    }
}
