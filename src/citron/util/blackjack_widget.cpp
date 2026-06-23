#include "blackjack_widget.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <random>
#include "citron/theme.h"

BlackjackWidget::BlackjackWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    m_hand_value_label = new QLabel(this);
    m_hand_value_label->setAlignment(Qt::AlignCenter);
    m_hand_value_label->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: #00ff00; margin-bottom: 5px;"));

    m_status_label = new QLabel(tr("Blackjack! Beat the dealer to win a surprise."), this);
    m_status_label->setAlignment(Qt::AlignCenter);
    
    auto* btn_layout = new QHBoxLayout();
    m_hit_button = new QPushButton(tr("Hit"), this);
    m_stand_button = new QPushButton(tr("Stand"), this);
    
    const bool is_dark = Theme::IsDarkMode();
    const QString accent = Theme::GetAccentColor();
    
    m_status_label->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold; color: %1;")
                                  .arg(is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e")));

    const QString btn_bg = is_dark ? QStringLiteral("#32323a") : QStringLiteral("#f0f0f5");
    const QString btn_fg = is_dark ? QStringLiteral("white") : QStringLiteral("#1a1a1e");
    const QString btn_border = is_dark ? QStringLiteral("#42424a") : QStringLiteral("#d0d0d5");
    const QString btn_hover_bg = is_dark ? QStringLiteral("#3d3d45") : QStringLiteral("#e0e0e5");
    const QString btn_disabled_bg = is_dark ? QStringLiteral("#1a1a1e") : QStringLiteral("#e8e8ed");
    const QString btn_disabled_fg = is_dark ? QStringLiteral("#555555") : QStringLiteral("#aaaaaa");
    const QString btn_disabled_border = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#dcdce0");
    
    // Calculate contrast color for when the button is pressed (using accent color)
    const QColor accent_color(accent);
    const double accent_lum = (0.299 * accent_color.red() + 0.587 * accent_color.green() + 0.114 * accent_color.blue()) / 255.0;
    const QString btn_pressed_fg = accent_lum > 0.65 ? QStringLiteral("black") : QStringLiteral("white");

    const QString btn_style = QStringLiteral(
        "QPushButton { background-color: %2; color: %3; border: 1px solid %4; "
        "border-radius: 12px; padding: 12px 32px; font-weight: bold; font-size: 15px; } "
        "QPushButton:hover { background-color: %5; border-color: %1; } "
        "QPushButton:pressed { background-color: %1; color: %6; } "
        "QPushButton:disabled { background-color: %7; color: %8; border: 1px solid %9; }"
    ).arg(accent, btn_bg, btn_fg, btn_border, btn_hover_bg, btn_pressed_fg, btn_disabled_bg, btn_disabled_fg, btn_disabled_border);
    
    m_hit_button->setStyleSheet(btn_style);
    m_stand_button->setStyleSheet(btn_style);

    btn_layout->addWidget(m_hit_button);
    btn_layout->addWidget(m_stand_button);

    m_animation_timer = new QTimer(this);
    connect(m_animation_timer, &QTimer::timeout, this, [this] {
        bool any = false;
        auto step = [&](std::vector<Card>& hand) {
            for (auto& c : hand) {
                if (c.anim_progress < 1.0f) {
                    c.anim_progress = std::min(1.0f, c.anim_progress + 0.12f);
                    any = true;
                }
            }
        };
        step(m_player_hand);
        step(m_dealer_hand);
        if (any) {
            update();
        } else {
            m_animation_timer->stop();
        }
    });

    m_dealer_timer = new QTimer(this);
    connect(m_dealer_timer, &QTimer::timeout, this, &BlackjackWidget::dealerStep);

    layout->addStretch();
    layout->addWidget(m_hand_value_label);
    layout->addWidget(m_status_label);
    layout->addLayout(btn_layout);

    connect(m_hit_button, &QPushButton::clicked, this, &BlackjackWidget::onHit);
    connect(m_stand_button, &QPushButton::clicked, this, &BlackjackWidget::onStand);
}

void BlackjackWidget::setGames(const std::vector<QImage>& icons) {
    m_games = icons;
}

void BlackjackWidget::reset() {
    m_player_hand.clear();
    m_dealer_hand.clear();
    m_is_game_over = false;
    m_hit_button->setEnabled(true);
    m_stand_button->setEnabled(true);
    m_status_label->setText(tr("Your turn. Hit or Stand?"));
    m_dealer_timer->stop();

    m_player_hand.push_back(drawCard());
    m_player_hand.back().anim_progress = 0.0f;
    m_player_hand.push_back(drawCard());
    m_player_hand.back().anim_progress = 0.0f;
    
    m_dealer_hand.push_back(drawCard());
    m_dealer_hand.back().anim_progress = 0.0f;
    
    Card hidden = drawCard();
    hidden.hidden = true;
    hidden.anim_progress = 0.0f;
    m_dealer_hand.push_back(hidden);

    m_animation_timer->start(16);
    m_hand_value_label->setText(tr("Your Hand: %1").arg(calculateHandValue(m_player_hand)));
    update();
}

BlackjackWidget::Card BlackjackWidget::drawCard() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> val_dist(1, 13);
    std::uniform_int_distribution<> suit_dist(0, 3);
    static const QStringList suits = {QStringLiteral("H"), QStringLiteral("D"), QStringLiteral("C"),
                                      QStringLiteral("S")};
    int val = val_dist(gen);
    return {val, suits[suit_dist(gen)], false};
}

int BlackjackWidget::calculateHandValue(const std::vector<Card>& hand) const {
    int total = 0, aces = 0;
    for (const auto& c : hand) {
        if (c.hidden)
            continue;
        int card_val = c.value > 10 ? 10 : c.value;
        if (card_val == 1)
            aces++;
        total += card_val;
    }
    for (int i = 0; i < aces; ++i)
        if (total + 10 <= 21)
            total += 10;
    return total;
}

void BlackjackWidget::onHit() {
    m_player_hand.push_back(drawCard());
    m_player_hand.back().anim_progress = 0.0f;
    
    int val = calculateHandValue(m_player_hand);
    m_hand_value_label->setText(tr("Your Hand: %1").arg(val));

    if (val > 21) {
        endHand(tr("Bust! Dealer wins."), false);
    }
    
    m_animation_timer->start(16);
    update();
}

void BlackjackWidget::onStand() {
    m_hit_button->setEnabled(false);
    m_stand_button->setEnabled(false);
    
    // Reveal dealer's hidden card with animation
    for (auto& c : m_dealer_hand) {
        if (c.hidden) {
            c.hidden = false;
            c.anim_progress = 0.0f; 
        }
    }
    m_animation_timer->start(16);
    
    m_dealer_timer->start(800);
}

void BlackjackWidget::dealerStep() {
    int val = calculateHandValue(m_dealer_hand);
    if (val < 17) {
        m_dealer_hand.push_back(drawCard());
        m_dealer_hand.back().anim_progress = 0.0f;
        m_animation_timer->start(16);
        update();
    } else {
        m_dealer_timer->stop();
        
        int p = calculateHandValue(m_player_hand);
        int d = calculateHandValue(m_dealer_hand);
        
        if (d > 21)
            endHand(tr("Dealer Busts! You win!"), true);
        else if (p > d)
            endHand(tr("You win! Selecting game..."), true);
        else if (p < d)
            endHand(tr("Dealer wins."), false);
        else
            endHand(tr("Push! Try again."), false);
    }
}

void BlackjackWidget::endHand(const QString& msg, bool win) {
    m_status_label->setText(msg);
    m_is_game_over = true;
    m_hit_button->setEnabled(false);
    m_stand_button->setEnabled(false);
    m_dealer_timer->stop();

    if (win && !m_games.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> d(0, static_cast<int>(m_games.size() - 1));
        emit gameSelected(d(gen));
    }
    update();
}

void BlackjackWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto drawHand = [&](std::vector<Card>& hand, int y_base, const QString& label) {
        p.setPen(Theme::IsDarkMode() ? Qt::white : QColor(26, 26, 30));
        p.setFont(QFont(QStringLiteral("sans-serif"), 12, QFont::Bold));

        qreal total_hand_width = hand.size() * 85 - 15;
        qreal start_x = (width() - total_hand_width) / 2.0;

        p.setOpacity(1.0);
        p.drawText(start_x, y_base - 20, label);

        for (size_t i = 0; i < hand.size(); ++i) {
            auto& card = hand[i];
            
            float slide_offset = (1.0f - card.anim_progress) * 45.0f;
            float opacity = card.anim_progress;
            
            p.setOpacity(opacity);
            QRectF r(start_x + i * 85, y_base + slide_offset, 70, 100);

            // Card shadow
            p.setBrush(QColor(0, 0, 0, 75));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r.translated(4, 4), 12, 12);

            // Card body
            if (card.hidden) {
                QLinearGradient back_grad(r.topLeft(), r.bottomRight());
                back_grad.setColorAt(0, QColor(50, 50, 90));
                back_grad.setColorAt(1, QColor(30, 30, 60));
                p.setBrush(back_grad);
                p.setPen(QPen(QColor(90, 90, 190), 2));
            } else {
                QLinearGradient face_grad(r.topLeft(), r.bottomRight());
                face_grad.setColorAt(0, Qt::white);
                face_grad.setColorAt(1, QColor(240, 240, 245));
                p.setBrush(face_grad);
                p.setPen(QPen(QColor(180, 180, 180), 1));
            }
            p.drawRoundedRect(r, 12, 12);

            if (!card.hidden) {
                bool is_red = card.suit == QStringLiteral("H") || card.suit == QStringLiteral("D");
                p.setPen(is_red ? QColor(255, 50, 50) : QColor(20, 20, 20));

                QString val_str;
                switch (card.value) {
                case 1: val_str = QStringLiteral("A"); break;
                case 11: val_str = QStringLiteral("J"); break;
                case 12: val_str = QStringLiteral("Q"); break;
                case 13: val_str = QStringLiteral("K"); break;
                default: val_str = QString::number(card.value); break;
                }

                QString suit_sym;
                if (card.suit == QStringLiteral("H")) suit_sym = QStringLiteral("♥");
                else if (card.suit == QStringLiteral("D")) suit_sym = QStringLiteral("♦");
                else if (card.suit == QStringLiteral("C")) suit_sym = QStringLiteral("♣");
                else if (card.suit == QStringLiteral("S")) suit_sym = QStringLiteral("♠");

                p.setFont(QFont(QStringLiteral("sans-serif"), 15, QFont::Bold));
                p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft, val_str);

                p.setFont(QFont(QStringLiteral("sans-serif"), 28));
                p.drawText(r.translated(1, 2), Qt::AlignCenter, suit_sym);

                p.setFont(QFont(QStringLiteral("sans-serif"), 15, QFont::Bold));
                p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignBottom | Qt::AlignRight, val_str);
            } else {
                p.setPen(QPen(QColor(255, 255, 255, 25), 1));
                for (int j = 0; j < 70; j += 10) {
                    p.drawLine(r.left() + j, r.top(), r.left(), r.top() + j);
                    p.drawLine(r.right() - j, r.bottom(), r.right(), r.bottom() - j);
                }
            }
        }
    };

    p.setOpacity(1.0);
    // Use fixed vertical offsets to ensure both hands are always visible and well-spaced
    // This prevents the dealer's hand from clipping off the top on smaller dialogs
    drawHand(m_dealer_hand, 40, tr("Dealer:"));
    drawHand(m_player_hand, 180, tr("You:"));
}
