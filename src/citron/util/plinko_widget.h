#pragma once

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <random>

class PlinkoWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlinkoWidget(QWidget* parent = nullptr);

    void setGames(const std::vector<QImage>& icons) {
        m_game_images = icons;
        update();
    }

    void reset() {
        m_can_drop = true;
        m_ball_pos = QPointF(width() / 2, 40);
        m_ball_vel = QPointF(0, 0);
        m_is_animating = false;
        if (m_timer->isActive()) m_timer->stop();
        update();
    }
    void dropBall(qreal startX);
    bool isAnimating() const { return m_is_animating; }

signals:
    void gameSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updatePhysics();

private:
    void setupPegs();
    void resolveCollision();

    struct Peg {
        QPointF pos;
        qreal radius = 4.0;
    };

    std::vector<Peg> m_pegs;
    std::vector<QImage> m_game_images;
    
    QPointF m_ball_pos = QPointF(250, 40);
    QPointF m_ball_vel = QPointF(0, 0);
    bool m_is_animating = false;
    bool m_can_drop = true;

    QTimer* m_timer;
};
