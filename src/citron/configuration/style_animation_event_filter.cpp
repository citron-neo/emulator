// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <QCoreApplication>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QWidget>

#include "citron/configuration/style_animation_event_filter.h"
#include "citron/uisettings.h"

class PaddingAnimator : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padding READ GetPadding WRITE SetPadding)

public:
    explicit PaddingAnimator(QPushButton* button) : QObject(button), target_button(button) {}

    void SetPadding(int padding) {
        current_padding = padding;
        target_button->setProperty("dynamicPadding", padding);
        target_button->setContentsMargins(padding, 0, 0, 0);
    }

    int GetPadding() const {
        return current_padding;
    }

private:
    QPushButton* target_button;
    int current_padding{10};
};

StyleAnimationEventFilter::StyleAnimationEventFilter(QObject* parent) : QObject(parent) {}
StyleAnimationEventFilter::~StyleAnimationEventFilter() = default;

void StyleAnimationEventFilter::animatePadding(QPushButton* button, int end) {
    if (animations.contains(button)) {
        auto* old = animations.take(button);
        old->stop();
        old->deleteLater();
    }

    int current = button->property("dynamicPadding").toInt();
    if (current == 0)
        current = 10;

    auto* animator = new PaddingAnimator(button);
    auto* animation = new QPropertyAnimation(animator, "padding", this);
    animator->setParent(animation);

    animation->setDuration(250);
    animation->setStartValue(current);
    animation->setEndValue(end);
    animation->setEasingCurve(QEasingCurve::OutCubic);

    animations.insert(button, animation);
    connect(animation, &QAbstractAnimation::finished, this,
            [this, button]() { animations.remove(button); });

    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void StyleAnimationEventFilter::animateClick(QPushButton* button, bool pressed) {
    if (click_animations.contains(button)) {
        auto* old = click_animations.take(button);
        old->stop();
        old->deleteLater();
    }

    if (pressed) {
        // Electrification is now manually triggered by the Dialog
    }

    int start_margin = pressed ? 0 : 4;
    int end_margin = pressed ? 4 : 0;

    auto* anim = new QVariantAnimation(this);
    anim->setDuration(100);
    anim->setStartValue(start_margin);
    anim->setEndValue(end_margin);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    connect(anim, &QVariantAnimation::valueChanged, this, [button](const QVariant& value) {
        int m = value.toInt();
        int p = button->property("dynamicPadding").toInt();
        if (p == 0)
            p = 10;
        button->setContentsMargins(p, m, 0, m);
    });

    click_animations.insert(button, anim);
    connect(anim, &QAbstractAnimation::finished, this,
            [this, button]() { click_animations.remove(button); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void StyleAnimationEventFilter::triggerElectrification(QPushButton* from, QPushButton* to) {
    auto* sidebar = qobject_cast<QWidget*>(to->parent());
    if (!sidebar)
        return;

    if (to->width() <= 0 || to->height() <= 0)
        return;

    if (!m_overlay) {
        m_overlay = new ElectricSparkOverlay(sidebar);
    } else if (m_overlay->parentWidget() != sidebar) {
        m_overlay->setParent(sidebar);
        m_overlay->show();
    }

    m_overlay->setGeometry(sidebar->rect());
    m_overlay->raise();

    QPoint p1 = from ? from->mapTo(sidebar, from->rect().center())
                     : to->mapTo(sidebar, to->rect().center());
    QPoint p2 = to->mapTo(sidebar, to->rect().center());

    if (p1.manhattanLength() < 5 || (p1 - p2).manhattanLength() < 10)
        p1 = p2;

    m_overlay->startArc(p1, p2, to);
}
void StyleAnimationEventFilter::triggerInitialState(QPushButton* first_button) {
    if (!first_button)
        return;

    // Use a zero-timer to ensure layout has settled before we calculate positions
    QTimer::singleShot(0, first_button,
                       [this, first_button]() { triggerElectrification(nullptr, first_button); });
}

void StyleAnimationEventFilter::triggerPageLightning(QWidget* parent, const QPoint& origin) {
    QWidget* window = parent->window();
    if (!m_page_overlay) {
        m_page_overlay = new LightningStrikeOverlay(window);
    } else if (m_page_overlay->parentWidget() != window) {
        m_page_overlay->setParent(window);
        m_page_overlay->show();
    }

    m_page_overlay->setGeometry(window->rect());
    m_page_overlay->raise();
    m_page_overlay->strike(origin);
}

bool StyleAnimationEventFilter::eventFilter(QObject* watched, QEvent* event) {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (!button)
        return false;

    switch (event->type()) {
    case QEvent::Enter:
        animatePadding(button, 18);
        break;
    case QEvent::Leave:
        animatePadding(button, 10);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

LightningStrikeOverlay::LightningStrikeOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(300);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        m_progress = val.toFloat();
        update();
    });
}

void LightningStrikeOverlay::strike(const QPoint& origin) {
    m_origin = origin;
    m_active = true;
    m_bolts.clear();

    int center_x = origin.x() > 0 ? origin.x() : rect().width() / 2;
    int end_x = center_x + QRandomGenerator::global()->bounded(-150, 151);

    // Heaven to Earth main bolt
    QPoint start(center_x, -150);
    QPoint end(end_x, rect().height() + 150);

    // 1. Primary massive bolt
    Bolt main_bolt;
    generateFractalBolt(start, end, 180.0f, 6, main_bolt.path);
    main_bolt.thickness_mult = 1.0f;
    m_bolts.push_back(main_bolt);

    // 2. Occasional branches
    for (int i = 0; i < 2; ++i) {
        float f = 0.3f + i * 0.3f;
        QPoint p1 = start + (end - start) * f;
        QPoint p2 = p1 + QPoint(QRandomGenerator::global()->bounded(-300, 301),
                                QRandomGenerator::global()->bounded(200, 401));

        Bolt branch;
        generateFractalBolt(p1, p2, 80.0f, 4, branch.path);
        branch.thickness_mult = 0.4f;
        m_bolts.push_back(branch);
    }

    m_anim->stop();
    m_anim->setDuration(450);
    m_anim->setStartValue(0.0f);
    m_anim->setEndValue(1.0f);
    m_anim->start();
    show();
}

void LightningStrikeOverlay::generateFractalBolt(const QPoint& p1, const QPoint& p2,
                                                 float displacement, int depth,
                                                 QPainterPath& path) {
    if (depth <= 0) {
        if (path.isEmpty()) {
            path.moveTo(p1);
        }
        path.lineTo(p2);
    } else {
        QPoint mid = (p1 + p2) / 2;

        // Perpendicular offset
        float dx = p2.x() - p1.x();
        float dy = p2.y() - p1.y();
        float len = sqrt(dx * dx + dy * dy);

        if (len > 0) {
            float nx = -dy / len;
            float ny = dx / len;
            float offset =
                (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * displacement;
            mid += QPoint(nx * offset, ny * offset);
        }

        generateFractalBolt(p1, mid, displacement * 0.5f, depth - 1, path);
        generateFractalBolt(mid, p2, displacement * 0.5f, depth - 1, path);
    }
}

void LightningStrikeOverlay::paintEvent(QPaintEvent* event) {
    if (!m_active)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = QColor(QString::fromStdString(UISettings::values.accent_color.GetValue()));
    if (!accent.isValid())
        accent = QColor(0, 200, 255);

    float alpha = 1.0f - m_progress;

    // 1. BLINDING DISCHARGE FLASH (Full screen)
    if (m_progress < 0.5f) {
        float flash_a =
            (m_progress < 0.1f) ? (m_progress / 0.1f) : (1.0f - (m_progress - 0.1f) / 0.4f);

        QColor flash_c = Qt::white;
        flash_c.setAlphaF(flash_a * 0.65f);
        painter.fillRect(rect(), flash_c);

        flash_c = accent;
        flash_c.setAlphaF(flash_a * 0.25f);
        painter.fillRect(rect(), flash_c);
    }

    for (const auto& bolt : m_bolts) {
        QColor c = accent;

        // Base Bloom Glow
        c.setAlphaF(alpha * 0.25f);
        painter.setPen(
            QPen(c, 24.0f * bolt.thickness_mult, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(bolt.path);

        // Core Energy Glow
        c.setAlphaF(alpha * 0.75f);
        painter.setPen(QPen(c.lighter(130), 8.0f * bolt.thickness_mult, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawPath(bolt.path);

        // Hot White Core
        c = Qt::white;
        c.setAlphaF(alpha);
        painter.setPen(
            QPen(c, 1.8f * bolt.thickness_mult, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(bolt.path);
    }
}

ElectricSparkOverlay::ElectricSparkOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(450);
    m_anim->setEasingCurve(QEasingCurve::OutExpo);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        m_progress = val.toFloat();
        update();
    });

    m_frame_timer = new QTimer(this);
    m_frame_timer->setInterval(16);
    connect(m_frame_timer, &QTimer::timeout, this, [this]() {
        m_border_runner_pos += 0.015f;
        if (m_border_runner_pos > 1.0f)
            m_border_runner_pos -= 1.0f;
        if (m_target_button || m_active)
            update();
    });
    m_frame_timer->start();
}

void ElectricSparkOverlay::startArc(const QPoint& from, const QPoint& to, QPushButton* target) {
    m_from = from;
    m_to = to;
    m_target_button = target;
    m_active = true;
    m_anim->stop();
    m_anim->setStartValue(0.0f);
    m_anim->setEndValue(1.2f);
    m_anim->start();
    show();
}

void ElectricSparkOverlay::drawElectricLine(QPainter& painter, const QPoint& p1, const QPoint& p2,
                                            const QColor& color, float thickness,
                                            float alpha_mult) {
    if (p1 == p2 || (p1 - p2).manhattanLength() < 2)
        return;
    QPainterPath path;
    path.moveTo(p1);

    int segments = 16;
    for (int i = 1; i <= segments; ++i) {
        float p = (float)i / segments;
        QPoint target = p1 + (p2 - p1) * p;

        int jitter = 5;
        int ox = (i == segments) ? 0 : QRandomGenerator::global()->bounded(-jitter, jitter + 1);
        int oy = (i == segments) ? 0 : QRandomGenerator::global()->bounded(-jitter, jitter + 1);

        if (segments > 8) {
            float wave = sin(p * M_PI) * 8.0f;
            ox += wave;
        }

        path.lineTo(target.x() + ox, target.y() + oy);
    }
    path.lineTo(p2);

    QColor c = color;
    c.setAlphaF(0.25f * alpha_mult);
    painter.setPen(QPen(c, thickness * 2.8f)); // Soft glow
    painter.drawPath(path);

    c = color.lighter(150);
    c.setAlphaF(0.8f * alpha_mult);
    painter.setPen(QPen(c, thickness * 0.8f)); // Subtle core
    painter.drawPath(path);
}

void ElectricSparkOverlay::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = QColor(QString::fromStdString(UISettings::values.accent_color.GetValue()));
    if (!accent.isValid())
        accent = QColor(0, 200, 255);

    // 1. Initial High-Voltage Contact Flash & Sparks (Localized to button)
    if (m_active && m_progress > 0.8f && m_progress < 1.3f) {
        float pulse = (m_progress - 0.8f) / 0.5f;
        float alpha = (pulse < 0.2f) ? (pulse * 5.0f) : (1.0f - (pulse - 0.2f) / 0.8f);

        // A) Core Bloom
        QColor flash_color = Qt::white;
        flash_color.setAlphaF(qMax(0.0f, alpha * 0.7f));
        painter.setBrush(flash_color);
        painter.setPen(Qt::NoPen);

        QPoint pos = m_target_button->mapTo(parentWidget(), QPoint(0, 0));
        QRect target_rect(pos, m_target_button->size());
        painter.drawRoundedRect(target_rect.adjusted(-2, -2, 2, 2), 8, 8);

        // B) Discharge Sparks
        if (pulse < 0.6f) {
            QPoint center = target_rect.center();
            for (int i = 0; i < 8; ++i) {
                float angle = (float)QRandomGenerator::global()->generateDouble() * 2.0f * M_PI;
                float dist = 15.0f + QRandomGenerator::global()->bounded(20);
                QPoint offset(cos(angle) * dist, sin(angle) * dist);

                QColor spark_c = (i % 2 == 0) ? Qt::white : accent;
                spark_c.setAlphaF(alpha);
                painter.setPen(QPen(spark_c, 1.5f));
                painter.drawLine(center + offset * 0.4f, center + offset);
            }
        }
    }

    // 2. CONNECTING ARC ANIMATION (Tracer arcs between tabs)
    if (m_active && m_progress < 1.0f && (m_from - m_to).manhattanLength() > 20 &&
        m_from.manhattanLength() > 10) {
        float arc_weights[] = {1.2f, 0.8f, 0.5f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            float alpha = (1.0f - m_progress) * 0.7f;
            QPoint spread(QRandomGenerator::global()->bounded(-6, 7),
                          QRandomGenerator::global()->bounded(-6, 7));
            drawElectricLine(painter, m_from, m_from + (m_to - m_from) * m_progress + spread,
                             accent, arc_weights[i], alpha);
        }
    }

    if (m_target_button) {
        QPoint pos = m_target_button->mapTo(parentWidget(), QPoint(0, 0));
        QRect r(pos, m_target_button->size());

        if (r.isEmpty())
            return;

        // Pulsating Power Glow (Overcharge breathing)
        float pulse = (sin(m_border_runner_pos * 2.0f * M_PI) + 1.0f) * 0.5f;
        QColor pulse_color = accent;
        pulse_color.setAlphaF(0.1f + pulse * 0.15f);
        painter.setBrush(pulse_color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(r.adjusted(-4, -4, 4, 4), 8, 8);

        // A) 'Overcharged' Edge Squiggles (4 concurrent arcs)
        for (int i = 0; i < 4; ++i) {
            int side = QRandomGenerator::global()->bounded(4);
            QPoint p1, p2;
            if (side == 0) {
                p1 = r.topLeft();
                p2 = r.topRight();
            } else if (side == 1) {
                p1 = r.topRight();
                p2 = r.bottomRight();
            } else if (side == 2) {
                p1 = r.bottomLeft();
                p2 = r.bottomRight();
            } else {
                p1 = r.topLeft();
                p2 = r.bottomLeft();
            }

            // Higher frequency jitter for overcharge feel
            drawElectricLine(painter, p1, p2, accent, 0.9f, 0.5f + pulse * 0.3f);
        }

        QPainterPath border_path;
        border_path.addRoundedRect(r.adjusted(-2, -2, 2, 2), 6, 6);

        float length = border_path.length();
        if (length <= 1.0f)
            return;

        float segment_len = length * 0.15f;
        float start_distance = m_border_runner_pos * length;

        QPainterPath dash_path;
        dash_path.moveTo(border_path.pointAtPercent(start_distance / length));
        for (float step = 0; step < segment_len; step += 2.0f) {
            float d = fmod(start_distance + step, length);
            dash_path.lineTo(border_path.pointAtPercent(d / length));
        }

        QColor runner_c = accent;
        runner_c.setAlphaF(0.8f);

        QColor glow = accent;
        glow.setAlphaF(0.3f);
        painter.setPen(QPen(glow, 5.0f));
        painter.drawPath(dash_path);

        painter.setPen(QPen(runner_c, 2.5f));
        painter.drawPath(dash_path);
    }
}

#include "citron/configuration/style_animation_event_filter.moc"
