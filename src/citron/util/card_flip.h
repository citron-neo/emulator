// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QImage>
#include <QPropertyAnimation>
#include <QWidget>
#include <vector>

class CardFlipWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal flipRotation READ flipRotation WRITE setFlipRotation)

public:
    explicit CardFlipWidget(QWidget* parent = nullptr);

    void setGames(const std::vector<QImage>& icons);
    void reset();

    qreal flipRotation() const { return m_flip_rotation; }
    void setFlipRotation(qreal rotation);

signals:
    void gameSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Card {
        QRectF rect;
        QImage icon;
        bool flipped = false;
        bool selected = false;
    };

    std::vector<Card> m_cards;
    int m_selected_index = -1;
    qreal m_flip_rotation = 0; // 0 to 180
    QPropertyAnimation* m_animation;
};
