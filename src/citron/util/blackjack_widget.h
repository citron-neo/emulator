#pragma once

#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

class BlackjackWidget : public QWidget {
    Q_OBJECT

public:
    explicit BlackjackWidget(QWidget* parent = nullptr);

    void setGames(const std::vector<QImage>& icons);
    void reset();

signals:
    void gameSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onHit();
    void onStand();
    void dealerStep();

private:
    struct Card {
        int value;
        QString suit;
        bool hidden = false;
        float anim_progress = 1.0f;
    };

    int calculateHandValue(const std::vector<Card>& hand) const;
    void endHand(const QString& message, bool playerWon);
    Card drawCard();

    std::vector<Card> m_player_hand;
    std::vector<Card> m_dealer_hand;
    std::vector<QImage> m_games;
    
    QLabel* m_hand_value_label;
    QLabel* m_status_label;
    QPushButton* m_hit_button;
    QPushButton* m_stand_button;
    QTimer* m_animation_timer;
    QTimer* m_dealer_timer;
    bool m_is_game_over = false;
};
