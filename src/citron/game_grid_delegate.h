#pragma once

#include <QAbstractItemView>
#include <QCache>
#include <QColor>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

class QListView;
class QTimer;

class GameGridDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum class GridMode {
        Grid,
        Carousel
    };

    explicit GameGridDelegate(QListView* view, QObject* parent = nullptr);
    ~GameGridDelegate() override;

    void setGridMode(GridMode mode);
    GridMode gridMode() const { return m_grid_mode; }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    void SetPopulating(bool populating);
    void RegisterEntryAnimation(const QModelIndex& index);
    void ClearAnimations();

private slots:
    void AdvanceAnimations();

private:
    void PaintGridItem(QPainter* painter, const QStyleOptionViewItem& option,
                       const QModelIndex& index) const;
    void PaintCarouselItem(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const;

    QColor CardBg() const;
    QColor TextColor() const;
    QColor DimColor() const;
    QColor SelectionColor() const;
    QColor AccentColor() const;

    QListView* m_view;
    GridMode m_grid_mode = GridMode::Grid;
    QTimer* m_animation_timer;
    qint64 m_pulse_tick = 0;
    bool m_is_populating = false;
    bool m_enable_bubble_animations = false;
    qreal m_population_fade_global = 1.0;

    mutable QMap<QPersistentModelIndex, qreal> m_hover_states;
    mutable QMap<QPersistentModelIndex, qreal> m_pulse_states;
    mutable QMap<QPersistentModelIndex, bool> m_pulse_direction;
    mutable QMap<QPersistentModelIndex, qreal> m_entry_animations;
    mutable QCache<QString, QIcon> m_greyscale_icon_cache;
};
