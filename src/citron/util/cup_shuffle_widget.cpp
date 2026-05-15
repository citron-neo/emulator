#include "cup_shuffle_widget.h"
#include <QPainter>
#include <QRandomGenerator>
#include <QMouseEvent>
#include <QTimer>
#include <cmath>
#include "citron/theme.h"

CupShuffleWidget::CupShuffleWidget(QWidget* parent) : QWidget(parent) {
    m_citron_logo.load(QStringLiteral(":/citron.svg"));

    auto* layout = new QVBoxLayout(this);
    m_status_label = new QLabel(tr("Find the game icon!"), this);
    m_status_label->setAlignment(Qt::AlignCenter);
    const bool is_dark = Theme::IsDarkMode();
    m_status_label->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; color: %1;")
                                  .arg(is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e")));

    m_start_button = new QPushButton(tr("Shuffle Cups"), this);
    const QString accent = Theme::GetAccentColor();
    const QColor accent_color(accent);
    const double accent_lum = (0.299 * accent_color.red() + 0.587 * accent_color.green() + 0.114 * accent_color.blue()) / 255.0;
    const QString btn_pressed_fg = accent_lum > 0.65 ? QStringLiteral("black") : QStringLiteral("white");

    const QString btn_bg = is_dark ? QStringLiteral("#262626") : QStringLiteral("#f0f0f5");
    const QString btn_fg = is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e");
    const QString btn_border = is_dark ? QStringLiteral("#404040") : QStringLiteral("#d0d0d5");
    const QString btn_hover_bg = is_dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e5");
    const QString btn_disabled_bg = is_dark ? QStringLiteral("#1a1a1a") : QStringLiteral("#e8e8ed");
    const QString btn_disabled_fg = is_dark ? QStringLiteral("#666") : QStringLiteral("#aaa");

    m_start_button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %2; color: %3; border: 1px solid %4; border-radius: 8px; padding: 12px; font-weight: bold; font-size: 16px; } "
        "QPushButton:hover { background-color: %5; } "
        "QPushButton:pressed { background-color: %1; color: %6; border-color: %1; } "
        "QPushButton:disabled { background-color: %7; color: %8; border: 1px solid %9; }"
    ).arg(accent, btn_bg, btn_fg, btn_border, btn_hover_bg, btn_pressed_fg, btn_disabled_bg, btn_disabled_fg, btn_border));

    layout->addStretch();
    layout->addWidget(m_status_label);
    layout->addWidget(m_start_button);

    m_shuffle_timer = new QTimer(this);
    connect(m_shuffle_timer, &QTimer::timeout, this, &CupShuffleWidget::onShuffle);

    m_cups.resize(3);
    reset();

    connect(m_start_button, &QPushButton::clicked, this, [this] {
        m_is_shuffling = true;
        m_ready_to_pick = false;
        m_shuffle_count = 0;
        m_max_shuffles = 8 + QRandomGenerator::global()->bounded(5);
        m_start_button->setEnabled(false);
        m_status_label->setText(tr("Watch closely..."));
        
        for (auto& c : m_cups) c.revealed = false;
        
        startRandomSwap();
        m_shuffle_timer->start(16);
    });
}

void CupShuffleWidget::setGames(const std::vector<QImage>& icons) {
    m_games = icons;
}

void CupShuffleWidget::reset() {
    m_is_shuffling = false;
    m_ready_to_pick = false;
    m_shuffle_timer->stop();
    m_start_button->setEnabled(true);
    m_status_label->setText(tr("Find the game icon!"));
    
    float spacing = 160.0f;
    for (int i = 0; i < 3; ++i) {
        m_cups[i].id = i;
        m_cups[i].x = (i - 1) * spacing;
        m_cups[i].target_x = m_cups[i].x;
        m_cups[i].has_ball = (i == 1); 
        m_cups[i].revealed = true;
    }
    update();
}

void CupShuffleWidget::startRandomSwap() {
    m_swap_a = QRandomGenerator::global()->bounded(3);
    m_swap_b = QRandomGenerator::global()->bounded(3);
    while (m_swap_a == m_swap_b) {
        m_swap_b = QRandomGenerator::global()->bounded(3);
    }
    
    m_cups[m_swap_a].target_x = m_cups[m_swap_b].x;
    m_cups[m_swap_b].target_x = m_cups[m_swap_a].x;
    m_anim_t = 0.0f;
}

void CupShuffleWidget::onShuffle() {
    if (!m_is_shuffling) return;

    m_anim_t += 0.08f; // Speed of the swap
    if (m_anim_t >= 1.0f) {
        m_anim_t = 1.0f;
        
        // Finalize position
        m_cups[m_swap_a].x = m_cups[m_swap_a].target_x;
        m_cups[m_swap_b].x = m_cups[m_swap_b].target_x;
        
        m_shuffle_count++;
        if (m_shuffle_count >= m_max_shuffles) {
            m_is_shuffling = false;
            m_ready_to_pick = true;
            m_shuffle_timer->stop();
            m_status_label->setText(tr("Pick a cup!"));
            m_start_button->setEnabled(true);
        } else {
            startRandomSwap();
        }
    }
    update();
}

void CupShuffleWidget::mousePressEvent(QMouseEvent* event) {
    if (!m_ready_to_pick || m_is_shuffling) return;
    
    int cx = width() / 2;
    int cy = height() / 2 - 50;
    
    for (auto& cup : m_cups) {
        QRect r(cx + cup.x - 55, cy - 55, 110, 110);
        if (r.contains(event->pos())) {
            cup.revealed = true;
            m_ready_to_pick = false;
            m_start_button->setEnabled(true);
            
            if (cup.has_ball) {
                m_status_label->setText(tr("Found it!"));
                if (!m_games.empty()) {
                    emit gameSelected(QRandomGenerator::global()->bounded(static_cast<int>(m_games.size())));
                }
            } else {
                m_status_label->setText(tr("Empty! Try again."));
                for (auto& c : m_cups) c.revealed = true;
            }
            update();
            break;
        }
    }
}

void CupShuffleWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int cx = width() / 2;
    int cy = height() / 2 - 50;

    auto getPos = [&](const Cup& c, int idx) -> QPointF {
        if (m_is_shuffling && (idx == m_swap_a || idx == m_swap_b)) {
            float start_x = (idx == m_swap_a) ? m_cups[m_swap_a].x : m_cups[m_swap_b].x;
            float end_x = (idx == m_swap_a) ? m_cups[m_swap_a].target_x : m_cups[m_swap_b].target_x;
            
            float cur_x = start_x + (end_x - start_x) * m_anim_t;
            // Arc effect
            float cur_y = -sin(m_anim_t * 3.14159f) * 40.0f; 
            return QPointF(cx + cur_x, cy + cur_y);
        }
        return QPointF(cx + c.x, cy);
    };

    for (int i = 0; i < 3; ++i) {
        const auto& cup = m_cups[i];
        QPointF pos = getPos(cup, i);
        QRectF r(pos.x() - 50, pos.y() - 50, 100, 100);
        
        if (cup.revealed && cup.has_ball) {
            QRadialGradient glow(r.center(), 70);
            glow.setColorAt(0, QColor(0, 150, 255, 120));
            glow.setColorAt(1, Qt::transparent);
            p.setBrush(glow);
            p.setPen(Qt::NoPen);
            p.drawEllipse(r.adjusted(-15, -15, 15, 15));
        }

        QLinearGradient grad(r.topLeft(), r.bottomRight());
        if (Theme::IsDarkMode()) {
            grad.setColorAt(0, QColor(70, 70, 85));
            grad.setColorAt(1, QColor(35, 35, 50));
            p.setPen(QPen(QColor(110, 110, 130), 2));
        } else {
            grad.setColorAt(0, Qt::white);
            grad.setColorAt(1, QColor(230, 230, 235));
            p.setPen(QPen(QColor(200, 200, 205), 2));
        }
        p.setBrush(grad);
        p.drawRoundedRect(r, 16, 16);
        
        if (cup.revealed) {
            if (cup.has_ball) {
                p.drawPixmap(r.adjusted(15, 15, -15, -15).toRect(), QPixmap::fromImage(m_citron_logo));
            } else {
                p.setPen(Theme::IsDarkMode() ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 50));
                p.setFont(QFont(QStringLiteral("sans-serif"), 32, QFont::Bold));
                p.drawText(r, Qt::AlignCenter, QStringLiteral("?"));
            }
        } else {
            p.setOpacity(0.4);
            p.drawPixmap(r.adjusted(25, 25, -25, -25).toRect(), QPixmap::fromImage(m_citron_logo));
            p.setOpacity(1.0);
        }
    }
}
