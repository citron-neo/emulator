// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QEvent>
#include <QWidget>
#include <QPainter>
#include <QColor>
#include <QPainterPath>
#include <QTimer>

class QPaintEvent;
class QPropertyAnimation;
class QAbstractAnimation;
class QVariantAnimation;
class QPushButton;
class ElectricSparkOverlay;
class LightningStrikeOverlay;

class StyleAnimationEventFilter final : public QObject {
    Q_OBJECT

public:
    explicit StyleAnimationEventFilter(QObject* parent = nullptr);
    ~StyleAnimationEventFilter() override;

    void triggerInitialState(QPushButton* first_button);
    void triggerElectrification(QPushButton* from, QPushButton* to);
    void triggerPageLightning(QWidget* parent, const QPoint& origin);


protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void animatePadding(QPushButton* button, int end);
    void animateClick(QPushButton* button, bool pressed);

    QHash<QPushButton*, QAbstractAnimation*> animations;
    QHash<QPushButton*, QAbstractAnimation*> click_animations;
    ElectricSparkOverlay* m_overlay{nullptr};
    LightningStrikeOverlay* m_page_overlay{nullptr};
};

class LightningStrikeOverlay : public QWidget {
    Q_OBJECT
public:
    explicit LightningStrikeOverlay(QWidget* parent);
    void strike(const QPoint& origin);

protected:
    void paintEvent(QPaintEvent* event) override;

    void generateFractalBolt(const QPoint& p1, const QPoint& p2, float displacement,
                             int depth, QPainterPath& path);

    QPoint m_origin;
    float m_progress{0.0f};
    bool m_active{false};
    QVariantAnimation* m_anim;
    struct Bolt {
        QPainterPath path;
        float thickness_mult;
    };
    std::vector<Bolt> m_bolts;
};

class ElectricSparkOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ElectricSparkOverlay(QWidget* parent);
    void startArc(const QPoint& from, const QPoint& to, QPushButton* target);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawElectricLine(QPainter& painter, const QPoint& p1, const QPoint& p2, 
                          const QColor& color, float thickness, float alpha_mult);

    QPoint m_from;
    QPoint m_to;
    QPushButton* m_target_button{nullptr};
    float m_progress{0.0f};
    float m_border_runner_pos{0.0f};
    bool m_active{false};
    QVariantAnimation* m_anim;
    QTimer* m_frame_timer;
};
