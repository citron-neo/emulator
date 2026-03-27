#include "blackjack_widget.h"
#include <QHBoxLayout>
#include <QPainter>
#include <random>

BlackjackWidget::BlackjackWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    m_hand_value_label = new QLabel(this);
    m_hand_value_label->setAlignment(Qt::AlignCenter);
    m_hand_value_label->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: #00ff00; margin-bottom: 5px;"));

    m_status_label = new QLabel(tr("Blackjack! Beat the dealer to win a surprise."), this);
    m_status_label->setAlignment(Qt::AlignCenter);
    m_status_label->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold; color: white;"));

    auto* btn_layout = new QHBoxLayout();
    m_hit_button = new QPushButton(tr("Hit"), this);
    m_stand_button = new QPushButton(tr("Stand"), this);
    btn_layout->addWidget(m_hit_button);
    btn_layout->addWidget(m_stand_button);

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

    m_player_hand.push_back(drawCard());
    m_player_hand.push_back(drawCard());
    m_dealer_hand.push_back(drawCard());
    Card hidden = drawCard();
    hidden.hidden = true;
    m_dealer_hand.push_back(hidden);

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
    if (calculateHandValue(m_player_hand) > 21)
        endHand(tr("Bust! Dealer wins."), false);
    
    m_hand_value_label->setText(tr("Your Hand: %1").arg(calculateHandValue(m_player_hand)));
    update();
}

void BlackjackWidget::onStand() {
    m_hit_button->setEnabled(false);
    m_stand_button->setEnabled(false);
    for (auto& c : m_dealer_hand)
        c.hidden = false;
    dealerTurn();
}

void BlackjackWidget::dealerTurn() {
    while (calculateHandValue(m_dealer_hand) < 17)
        m_dealer_hand.push_back(drawCard());
    int p = calculateHandValue(m_player_hand);
    int d = calculateHandValue(m_dealer_hand);
    if (d > 21 || p > d)
        endHand(tr("You win! Selecting game..."), true);
    else if (p < d)
        endHand(tr("Dealer wins."), false);
    else
        endHand(tr("Push! Try again."), false);
    update();
}

void BlackjackWidget::endHand(const QString& msg, bool win) {
    m_status_label->setText(msg);
    m_hand_value_label->setText(tr("Your Hand: %1").arg(calculateHandValue(m_player_hand)));
    m_is_game_over = true;
    m_hit_button->setEnabled(false);
    m_stand_button->setEnabled(false);
    if (win && !m_games.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> d(0, static_cast<int>(m_games.size() - 1));
        emit gameSelected(d(gen));
    }
}

void BlackjackWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto drawHand = [&](const std::vector<Card>& hand, int y, const QString& label) {
        p.setPen(Qt::white);
        p.setFont(QFont(QStringLiteral("sans-serif"), 10, QFont::Bold));
        
        qreal total_hand_width = hand.size() * 70 - 10;
        qreal start_x = std::max(20.0, (width() - total_hand_width) / 2.0);
        
        p.drawText(start_x, y - 10, label);
        
        for (size_t i = 0; i < hand.size(); ++i) {
            QRectF r(start_x + i * 70, y, 60, 90);
            
            // Card shadow - deeper and softer
            p.setBrush(QColor(0, 0, 0, 80));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r.translated(3, 3), 10, 10);

            // Card body with subtle depth
            if (hand[i].hidden) {
                QLinearGradient back_grad(r.topLeft(), r.bottomRight());
                back_grad.setColorAt(0, QColor(60, 60, 90));
                back_grad.setColorAt(1, QColor(40, 40, 60));
                p.setBrush(back_grad);
                p.setPen(QPen(QColor(100, 100, 180), 2));
            } else {
                QLinearGradient face_grad(r.topLeft(), r.bottomRight());
                face_grad.setColorAt(0, Qt::white);
                face_grad.setColorAt(1, QColor(240, 240, 245));
                p.setBrush(face_grad);
                p.setPen(QPen(QColor(180, 180, 180), 1));
            }
            p.drawRoundedRect(r, 10, 10);

            if (!hand[i].hidden) {
                bool is_red = hand[i].suit == QStringLiteral("H") || hand[i].suit == QStringLiteral("D");
                p.setPen(is_red ? QColor(200, 40, 40) : QColor(30, 30, 30));
                
                QString val_str;
                switch (hand[i].value) {
                case 1: val_str = QStringLiteral("A"); break;
                case 11: val_str = QStringLiteral("J"); break;
                case 12: val_str = QStringLiteral("Q"); break;
                case 13: val_str = QStringLiteral("K"); break;
                default: val_str = QString::number(hand[i].value); break;
                }

                QString suit_sym;
                if (hand[i].suit == QStringLiteral("H")) suit_sym = QStringLiteral("♥");
                else if (hand[i].suit == QStringLiteral("D")) suit_sym = QStringLiteral("♦");
                else if (hand[i].suit == QStringLiteral("C")) suit_sym = QStringLiteral("♣");
                else if (hand[i].suit == QStringLiteral("S")) suit_sym = QStringLiteral("♠");

                // Layout corners
                p.setFont(QFont(QStringLiteral("sans-serif"), 12, QFont::Bold));
                p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft, val_str);
                
                // Center suit - slightly larger and centered
                p.setFont(QFont(QStringLiteral("sans-serif"), 22));
                p.drawText(r.translated(1, 1), Qt::AlignCenter, suit_sym); 
                
                p.setFont(QFont(QStringLiteral("sans-serif"), 12, QFont::Bold));
                p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignBottom | Qt::AlignRight, val_str);
            } else {
                // Rich pattern on the back
                p.setPen(QPen(QColor(255, 255, 255, 25), 1));
                for (int j = 0; j < 60; j += 8) {
                    p.drawLine(r.left() + j, r.top(), r.left(), r.top() + j);
                    p.drawLine(r.right() - j, r.bottom(), r.right(), r.bottom() - j);
                }
            }
        }
    };
    drawHand(m_dealer_hand, 40, tr("Dealer:"));
    drawHand(m_player_hand, 160, tr("You:"));
}
