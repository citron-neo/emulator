// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/util/card_flip.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

CardFlipWidget::CardFlipWidget(QWidget* parent) : QWidget(parent) {
    m_animation = new QPropertyAnimation(this, "flipRotation", this);
    m_animation->setDuration(600);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
}

void CardFlipWidget::setGames(const std::vector<QImage>& icons) {
    m_cards.clear();
    m_selected_index = -1;
    m_flip_rotation = 0;

    const int n = icons.size();
    const qreal margin = 20;
    const qreal total_width = width() - (margin * 2);
    const qreal card_w = (total_width / n) - 10;
    const qreal card_h = card_w * 1.4;

    for (int i = 0; i < n; ++i) {
        Card c;
        c.icon = icons[i];
        c.rect = QRectF(margin + i * (card_w + 10), (height() - card_h) / 2, card_w, card_h);
        m_cards.push_back(c);
    }
    update();
}

void CardFlipWidget::reset() {
    for (auto& c : m_cards) {
        c.flipped = false;
        c.selected = false;
    }
    m_selected_index = -1;
    m_flip_rotation = 0;
    update();
}

void CardFlipWidget::setFlipRotation(qreal rotation) {
    m_flip_rotation = rotation;
    update();
}

void CardFlipWidget::mousePressEvent(QMouseEvent* event) {
    if (m_selected_index != -1) return;

    for (int i = 0; i < (int)m_cards.size(); ++i) {
        if (m_cards[i].rect.contains(event->pos())) {
            m_selected_index = i;
            m_cards[i].selected = true;
            m_animation->setStartValue(0);
            m_animation->setEndValue(180);
            m_animation->start();
            
            connect(m_animation, &QPropertyAnimation::finished, this, [this, i]() {
                emit gameSelected(i);
                m_animation->disconnect();
            });
            break;
        }
    }
}

void CardFlipWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < (int)m_cards.size(); ++i) {
        const auto& c = m_cards[i];
        painter.save();
        
        QTransform transform;
        transform.translate(c.rect.center().x(), c.rect.center().y());
        
        if (i == m_selected_index) {
            transform.rotate(m_flip_rotation, Qt::YAxis);
        }
        
        painter.setTransform(transform);
        QRectF draw_rect(-c.rect.width() / 2, -c.rect.height() / 2, c.rect.width(), c.rect.height());

        // Draw Card Back or Front
        bool show_front = (i == m_selected_index && m_flip_rotation > 90);
        
        QPainterPath path;
        path.addRoundedRect(draw_rect, 10, 10);
        
        if (!show_front) {
            // Card Back (Premium Dark Design)
            painter.fillPath(path, QColor(30, 30, 30));
            painter.setPen(QPen(QColor(60, 60, 60), 2));
            painter.drawPath(path);
            
            // Subtle Pattern
            painter.setPen(QPen(QColor(45, 45, 45), 1));
            painter.drawRect(draw_rect.adjusted(10, 10, -10, -10));
        } else {
            // Card Front
            painter.fillPath(path, Qt::white);
            painter.setPen(QPen(QColor(200, 200, 200), 1));
            painter.drawPath(path);
            
            // Flip the icon back
            painter.scale(-1, 1);
            painter.drawImage(draw_rect.adjusted(5, 5, -5, -5), c.icon);
        }
        
        painter.restore();
    }
}
