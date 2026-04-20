#pragma once

#include <QAbstractItemModel>
#include <QPropertyAnimation>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QSpacerItem>

class CinematicCarousel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal focalIndex READ focalIndex WRITE setFocalIndex)

public:
    explicit CinematicCarousel(QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model);
    qreal focalIndex() const { return m_focal_index; }
    void setFocalIndex(qreal index);

    QModelIndex currentIndex() const;
    QModelIndex indexAt(const QPoint& point) const;
    QRect visualRect(const QModelIndex& index) const;
    QWidget* viewport() const { return const_cast<CinematicCarousel*>(this); }
    QAbstractItemModel* model() const { return m_model; }

    void scrollTo(int index);
    void scrollToLetter(QChar letter);
    void ApplyTheme();

    void setControllerFocus(bool focus);
    bool hasControllerFocus() const { return m_has_focus; }
    
public slots:
    void onNavigated(int dx, int dy);
    void onActivated();
    void onCancelled();

signals:
    void focalItemChanged(const QModelIndex& index);
    void itemActivated(const QModelIndex& index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void startSnapAnimation(qreal target);
    void updateFocalItem();
    QModelIndex iconAt(const QPoint& point) const;

    QAbstractItemModel* m_model = nullptr;
    qreal m_focal_index = 0.0;
    QPropertyAnimation* m_snap_animation = nullptr;
    
    QTimer* m_pulse_timer = nullptr;
    QTimer* m_scroll_timer = nullptr;
    qint64 m_pulse_tick = 0;

    QPoint m_last_mouse_pos;
    QPoint m_drag_start_pos;
    bool m_is_dragging = false;

    bool m_left_arrow_hover = false;
    bool m_right_arrow_hover = false;
    int m_hover_icon_index = -1;

    QColor CardBg() const;
    QColor TextColor() const;
    QColor AccentColor() const;

    bool m_has_focus = false;
    
    // Momentum / Physics members
    QTimer* m_momentum_timer = nullptr;
    qreal m_velocity = 0.0;
    qint64 m_last_move_timestamp = 0;
};

class GameCarouselView : public QWidget {
    Q_OBJECT

public:
    explicit GameCarouselView(QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model);
    void ApplyTheme();
    
    CinematicCarousel* view() const { return m_carousel; }
    QAbstractItemModel* model() const { return m_carousel->model(); }

    void setControllerFocus(bool focus) { m_carousel->setControllerFocus(focus); }
    bool hasControllerFocus() const { return m_carousel->hasControllerFocus(); }

public slots:
    void onNavigated(int dx, int dy) { m_carousel->onNavigated(dx, dy); }
    void onActivated() { m_carousel->onActivated(); }
    void onCancelled() { m_carousel->onCancelled(); }

signals:
    void itemActivated(const QModelIndex& index);
    void itemSelectionChanged(const QModelIndex& index);
    void focusReturned();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    CinematicCarousel* m_carousel = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_top_hint = nullptr;
    QLabel* m_bottom_hint = nullptr;
};
