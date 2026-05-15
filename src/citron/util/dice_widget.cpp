#include "dice_widget.h"
#include <QPainter>
#include <QRandomGenerator>
#include <QHBoxLayout>
#include <QTimer>
#include "citron/theme.h"

DiceWidget::DiceWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);

    const bool is_dark = Theme::IsDarkMode();
    const QString accent = Theme::GetAccentColor();
    
    m_status_label = new QLabel(tr("Pick your bet and roll!"), this);
    m_status_label->setAlignment(Qt::AlignCenter);
    m_status_label->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; color: %1;")
                                  .arg(is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e")));

    auto* bet_layout = new QHBoxLayout();
    m_bet_small = new QPushButton(tr("Small (2-6)"), this);
    m_bet_mid = new QPushButton(tr("Seven (7)"), this);
    m_bet_big = new QPushButton(tr("Big (8-12)"), this);

    const QString btn_bg = is_dark ? QStringLiteral("#32323a") : QStringLiteral("#f0f0f5");
    const QString btn_fg = is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e");
    const QString btn_border = is_dark ? QStringLiteral("#42424a") : QStringLiteral("#d0d0d5");
    const QString btn_hover_bg = is_dark ? QStringLiteral("#42424a") : QStringLiteral("#e0e0e5");
    const QString btn_disabled_bg = is_dark ? QStringLiteral("#1a1a1e") : QStringLiteral("#e8e8ed");
    const QString btn_disabled_fg = is_dark ? QStringLiteral("#555") : QStringLiteral("#aaa");
    
    const QColor accent_color(accent);
    const double accent_lum = (0.299 * accent_color.red() + 0.587 * accent_color.green() + 0.114 * accent_color.blue()) / 255.0;
    const QString btn_checked_fg = accent_lum > 0.65 ? QStringLiteral("black") : QStringLiteral("white");

    const QString btn_style = QStringLiteral(
        "QPushButton { background-color: %2; color: %3; border: 1px solid %4; border-radius: 8px; padding: 8px; font-weight: bold; } "
        "QPushButton:hover { background-color: %5; } "
        "QPushButton:checked { background-color: %1; border-color: %1; color: %6; } "
        "QPushButton:disabled { background-color: %7; color: %8; border: 1px solid %9; }"
    ).arg(accent, btn_bg, btn_fg, btn_border, btn_hover_bg, btn_checked_fg, btn_disabled_bg, btn_disabled_fg, btn_border);

    for (auto* btn : {m_bet_small, m_bet_mid, m_bet_big}) {
        btn->setCheckable(true);
        btn->setStyleSheet(btn_style);
        bet_layout->addWidget(btn);
    }

    m_roll_button = new QPushButton(tr("Roll Dice"), this);
    m_roll_button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2e7d32; color: white; border-radius: 8px; padding: 12px; font-weight: bold; font-size: 16px; border: none; } "
        "QPushButton:hover { background-color: #388e3c; } "
        "QPushButton:disabled { background-color: %1; color: %2; }"
    ).arg(is_dark ? QStringLiteral("#1b5e20") : QStringLiteral("#a5d6a7"), 
          is_dark ? QStringLiteral("#aaa") : QStringLiteral("#e8f5e9")));
    m_roll_button->setEnabled(false);

    layout->addStretch();
    layout->addLayout(bet_layout);
    layout->addWidget(m_status_label);
    layout->addWidget(m_roll_button);

    m_roll_timer = new QTimer(this);
    connect(m_roll_timer, &QTimer::timeout, this, &DiceWidget::onRoll);

    connect(m_bet_small, &QPushButton::clicked, this, [this] { onBet(0); });
    connect(m_bet_mid, &QPushButton::clicked, this, [this] { onBet(2); });
    connect(m_bet_big, &QPushButton::clicked, this, [this] { onBet(1); });
    connect(m_roll_button, &QPushButton::clicked, this, [this] {
        m_is_rolling = true;
        m_roll_count = 0;
        m_roll_button->setEnabled(false);
        m_bet_small->setEnabled(false);
        m_bet_mid->setEnabled(false);
        m_bet_big->setEnabled(false);
        m_roll_timer->start(80);
    });
}

void DiceWidget::setGames(const std::vector<QImage>& icons) {
    m_games = icons;
}

void DiceWidget::reset() {
    m_is_rolling = false;
    m_bet_type = -1;
    m_bet_small->setChecked(false);
    m_bet_mid->setChecked(false);
    m_bet_big->setChecked(false);
    m_bet_small->setEnabled(true);
    m_bet_mid->setEnabled(true);
    m_bet_big->setEnabled(true);
    m_roll_button->setEnabled(false);
    m_status_label->setText(tr("Pick your bet and roll!"));
    update();
}

void DiceWidget::onBet(int type) {
    m_bet_type = type;
    m_bet_small->setChecked(type == 0);
    m_bet_big->setChecked(type == 1);
    m_bet_mid->setChecked(type == 2);
    m_roll_button->setEnabled(true);
}

void DiceWidget::onRoll() {
    m_die1 = QRandomGenerator::global()->bounded(1, 7);
    m_die2 = QRandomGenerator::global()->bounded(1, 7);
    m_roll_count++;
    
    if (m_roll_count > 15) {
        m_roll_timer->stop();
        endRoll();
    }
    update();
}

void DiceWidget::endRoll() {
    m_is_rolling = false;
    int sum = m_die1 + m_die2;
    bool win = false;
    
    if (m_bet_type == 0 && sum <= 6) win = true;
    else if (m_bet_type == 1 && sum >= 8) win = true;
    else if (m_bet_type == 2 && sum == 7) win = true;
    
    if (win) {
        m_status_label->setText(tr("Winner! Sum: %1").arg(sum));
        if (!m_games.empty()) {
            emit gameSelected(QRandomGenerator::global()->bounded(static_cast<int>(m_games.size())));
        }
    } else {
        m_status_label->setText(tr("Lost! Sum: %1").arg(sum));
    }
}

void DiceWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto drawDie = [&](int val, int x, int y) {
        QRect r(x, y, 100, 100);
        
        // Shadow
        p.setBrush(QColor(0, 0, 0, 80));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r.translated(5, 5), 15, 15);

        // Body
        QLinearGradient grad(r.topLeft(), r.bottomRight());
        grad.setColorAt(0, Qt::white);
        grad.setColorAt(1, QColor(220, 220, 230));
        p.setBrush(grad);
        p.setPen(QPen(QColor(180, 180, 180), 2));
        p.drawRoundedRect(r, 15, 15);

        // Dots
        p.setBrush(QColor(30, 30, 30));
        p.setPen(Qt::NoPen);
        
        int dot_s = 16;
        int m = 50;
        int l = 25;
        int r_ = 75;

        auto dot = [&](int dx, int dy) {
            p.drawEllipse(QPoint(x + dx, y + dy), dot_s/2, dot_s/2);
        };

        if (val % 2 == 1) dot(m, m);
        if (val >= 2) { dot(l, l); dot(r_, r_); }
        if (val >= 4) { dot(l, r_); dot(r_, l); }
        if (val == 6) { dot(l, m); dot(r_, m); }
    };

    int cy = (height() - 150) / 2;
    drawDie(m_die1, width() / 2 - 110, cy - 50);
    drawDie(m_die2, width() / 2 + 10, cy - 50);
}
