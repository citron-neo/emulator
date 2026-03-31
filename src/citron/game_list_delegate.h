// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QMap>
#include <QObject>
#include <QPersistentModelIndex>
#include <QSize>
#include <QStyledItemDelegate>
#include <QTimer>

class QTreeView;
class QPainter;
class QRect;
class QPixmap;
class QString;
class QModelIndex;
class QAbstractItemView;
class QStyleOptionViewItem;

/**
 * GameListDelegate — renders the Game List entries as modern gaming cards.
 * This modernizes the old Qt TreeView aesthetic to follow the Citron branded themeing.
 */
class GameListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit GameListDelegate(QTreeView* view, QObject* parent = nullptr);
    ~GameListDelegate() override;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    // Constants for layout
    static constexpr int kCardMarginV = 3;
    static constexpr int kCardRadius = 8;
    static constexpr int kLineSpacing = 4;

    // Helpers for dynamic sizing
    int GetCardHeight() const;
    int GetIconSize() const;

public slots:
    void AdvanceAnimations();

private:
    // ---- Per-column paint helpers ----
    void PaintBackground(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const;
    void PaintGameInfo(QPainter* painter, const QRect& rect, const QStyleOptionViewItem& option,
                       const QModelIndex& index) const;
    void PaintCompatibility(QPainter* painter, const QRect& rect,
                             const QModelIndex& index) const;
    void PaintDefault(QPainter* painter, const QRect& rect, const QStyleOptionViewItem& option,
                      const QModelIndex& index) const;

    // ---- Color helpers ----
    QColor CardBg() const;
    QColor TextColor() const;
    QColor DimColor() const;
    QColor SelectionColor() const;
    QColor AccentColor() const;

    // Animation and caching state
    mutable QMap<QPersistentModelIndex, int> vertical_scroll_offsets;
    mutable QMap<QPersistentModelIndex, int> vertical_scroll_pause;
    mutable QMap<QPersistentModelIndex, qreal> hover_states;
    mutable QMap<QPersistentModelIndex, qreal> pulse_states;
    mutable QMap<QPersistentModelIndex, bool> pulse_direction;
    
    // Performance optimizations
    mutable QMap<QString, QStringList> addons_item_cache;
    mutable QSet<QPersistentModelIndex> animating_indices;
    QTimer* animation_timer;
    QTreeView* tree_view;
};
