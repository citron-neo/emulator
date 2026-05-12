#pragma once

#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

class CupShuffleWidget : public QWidget {
    Q_OBJECT

public:
    explicit CupShuffleWidget(QWidget* parent = nullptr);

    void setGames(const std::vector<QImage>& icons);
    void reset();

signals:
    void gameSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onShuffle();

private:
    struct Cup {
        int id;
        float x;
        float target_x;
        bool has_ball = false;
        bool revealed = false;
    };
    
    std::vector<QImage> m_games;
    std::vector<Cup> m_cups;
    
    QTimer* m_shuffle_timer;
    int m_shuffle_count = 0;
    int m_max_shuffles = 0;
    bool m_is_shuffling = false;
    bool m_ready_to_pick = false;
    float m_anim_t = 1.0f; // 0.0 to 1.0 for the current swap
    
    int m_swap_a = -1;
    int m_swap_b = -1;
    
    QLabel* m_status_label;
    QPushButton* m_start_button;
    QImage m_citron_logo;

    void startRandomSwap();
};
