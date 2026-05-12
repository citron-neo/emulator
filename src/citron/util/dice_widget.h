#pragma once

#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

class DiceWidget : public QWidget {
    Q_OBJECT

public:
    explicit DiceWidget(QWidget* parent = nullptr);

    void setGames(const std::vector<QImage>& icons);
    void reset();

signals:
    void gameSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onRoll();
    void onBet(int type);

private:
    void endRoll();
    
    std::vector<QImage> m_games;
    
    int m_die1 = 1;
    int m_die2 = 1;
    int m_bet_type = -1; // 0: Small (2-6), 1: Big (8-12), 2: Mid (7)
    
    QTimer* m_roll_timer;
    int m_roll_count = 0;
    
    QLabel* m_status_label;
    QPushButton* m_roll_button;
    QPushButton* m_bet_small;
    QPushButton* m_bet_mid;
    QPushButton* m_bet_big;
    
    bool m_is_rolling = false;
};
