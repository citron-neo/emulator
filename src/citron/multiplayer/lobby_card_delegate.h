// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QMap>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QTimer>

class QTreeView;
class QPainter;
class QRect;
class QPixmap;
class QString;
class QModelIndex;
class QAbstractItemView;
class QHelpEvent;
class QStyleOptionViewItem;

/**
 * LobbyCardDelegate — renders Public Room Browser rows as modern gaming cards.
 *
 * Column layout (set in lobby.cpp, NOT here):
 *   GAME_NAME  128 px fixed   icon + card background spans full row
 *   ROOM_NAME  stretch        marquee title (only this shrinks on resize)
 *   MEMBER     133 px fixed   player count pill
 *   HOST       hidden
 *
 * Each cell is painted independently; the card background drawn in column 0
 * extends to the full viewport width so the row looks unified.
 */
class LobbyCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // ---- Public constants (used by lobby.cpp for header setup) ----
    static constexpr int kIconSize    = 60;  ///< Game icon edge (px)
    static constexpr int kCardHeight  = 76;  ///< Top-level row height
    static constexpr int kChildHeight = 26;  ///< Expanded member row height
    static constexpr int kCardRadius  = 7;   ///< Rounded corner radius
    static constexpr int kCardMarginV = 2;   ///< Vertical gap between cards

    static constexpr int kScrollPause = 90;  ///< Ticks to hold before scrolling
    static constexpr int kScrollSpeed = 1;   ///< Pixels per timer tick

    explicit LobbyCardDelegate(QTreeView* view, QObject* parent = nullptr);
    ~LobbyCardDelegate() override;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
    void  paint(QPainter*, const QStyleOptionViewItem&,
                const QModelIndex&) const override;
    bool  helpEvent(QHelpEvent*, QAbstractItemView*,
                    const QStyleOptionViewItem&, const QModelIndex&) override;

public slots:
    void AdvanceAnimations();
    void OnRowClicked(const QModelIndex&);
    void SetAnimateNames(bool animate);  ///< Connected to the "Scroll Names" checkbox

private:
    // ---- Per-column paint helpers ----
    /// Draw the card background spanning the full row (called for col 0 only).
    void PaintBackground(QPainter*, const QStyleOptionViewItem&,
                         const QModelIndex&) const;
    /// Draw the game icon centred in the 128 px cell (col 0).
    void PaintIcon(QPainter*, const QRect& cell_rect,
                   const QModelIndex&) const;
    /// Draw the marquee room name into the stretch cell (col ROOM_NAME).
    void PaintRoomName(QPainter*, const QRect& cell_rect,
                       const QStyleOptionViewItem&,
                       const QModelIndex&) const;
    /// Draw the player badge into the fixed 133 px cell (col MEMBER).
    void PaintBadge(QPainter*, const QRect& cell_rect,
                    const QModelIndex&) const;
    /// Draw a merged child-member card spanning from col 0 to viewport edge.
    void PaintChildCard(QPainter*, const QRect& full_rect,
                        const QStyleOptionViewItem&,
                        const QModelIndex&) const;

    // ---- Draw primitives ----
    void DrawRoundedIcon(QPainter*, const QRect&, const QPixmap&) const;
    void DrawPlayerBadge(QPainter*, const QRect&, int cur, int max) const;
    void DrawPulseBorder(QPainter*, const QRect&, qreal progress) const;
    void DrawHoverGlow  (QPainter*, const QRect&, qreal progress) const;

    // ---- Colour helpers ----
    QColor CardBg()      const;
    QColor ChildBg()     const;
    QColor FgColor()     const;
    QColor DimColor()    const;
    QColor AccentColor() const;
    QColor PlayerColor(int cur, int max) const;

    /// Canonical row key — always column 0 of the given index's row.
    static QPersistentModelIndex RowKey(const QModelIndex& idx) {
        return QPersistentModelIndex(idx.sibling(idx.row(), 0));
    }

    // ---- Private data ----
    QTreeView* tree_view;
    QTimer*    anim_timer;
    bool       animate_names = true;  ///< Controlled by the "Scroll Names" checkbox

    mutable QMap<QPersistentModelIndex, int>   scroll_offset;
    mutable QMap<QPersistentModelIndex, int>   scroll_pause_ticks;
    mutable QMap<QPersistentModelIndex, qreal> hover_prog;

    mutable QPersistentModelIndex click_idx;
    mutable qreal                 click_prog = 0.0;
};
