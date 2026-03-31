// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/multiplayer/lobby_card_delegate.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QLinearGradient>
#include <QModelIndex>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <QTreeView>

#include "citron/multiplayer/lobby_p.h"
#include "citron/uisettings.h"

// ============================================================
// Constructor / destructor
// ============================================================

LobbyCardDelegate::LobbyCardDelegate(QTreeView* view, QObject* parent)
    : QStyledItemDelegate(parent)
    , tree_view(view)
    , anim_timer(new QTimer(this)) {
    anim_timer->setInterval(40); // ~25 fps
    connect(anim_timer, &QTimer::timeout, this, &LobbyCardDelegate::AdvanceAnimations);
    anim_timer->start();
}

LobbyCardDelegate::~LobbyCardDelegate() = default;

// ============================================================
// sizeHint
// ============================================================

QSize LobbyCardDelegate::sizeHint(const QStyleOptionViewItem&,
                                   const QModelIndex& idx) const {
    return QSize(0, idx.parent().isValid() ? kChildHeight : kCardHeight);
}

// ============================================================
// paint  — dispatches per column
// ============================================================

void LobbyCardDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    // HOST column: never paint anything
    if (index.column() == Column::HOST) return;

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing |
                            QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);
    // Allow glow effects to bleed slightly outside individual cell rects
    if (tree_view) {
        painter->setClipRect(tree_view->viewport()->rect());
    }

    const bool is_child = index.parent().isValid();

    if (is_child) {
        // For child rows, only column 0 draws the merged spanning bar
        if (index.column() == Column::GAME_NAME) {
            QRect full_rect = option.rect;
            if (tree_view) full_rect.setRight(tree_view->viewport()->width() - 4);
            full_rect.adjust(0, kCardMarginV, 0, -kCardMarginV);
            PaintChildCard(painter, full_rect, option, index);
        }
        // Other child columns: transparent, nothing to draw
    } else {
        // Top-level room rows — each column draws its portion of the card
        switch (index.column()) {
        case Column::GAME_NAME:
            PaintBackground(painter, option, index);   // full-row card BG
            PaintIcon(painter, option.rect, index);    // centred icon
            break;
        case Column::ROOM_NAME:
            PaintRoomName(painter, option.rect, option, index);
            break;
        case Column::MEMBER:
            PaintBadge(painter, option.rect, index);
            break;
        default:
            break;
        }
    }

    painter->restore();
}

// ============================================================
// helpEvent  — rich tooltip on hover over any top-level cell
// ============================================================

bool LobbyCardDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index) {
    if (!index.isValid() || event->type() != QEvent::ToolTip) {
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
    if (index.parent().isValid()) {
        QToolTip::hideText();
        return true;
    }

    const QAbstractItemModel* m = index.model();
    const int  row  = index.row();
    const auto root = index.parent();
    auto sib = [&](int c){ return m->index(row, c, root); };

    const QString game_name =
        m->data(sib(Column::GAME_NAME), LobbyItemGame::GameNameRole).toString();
    const QString room_name =
        m->data(sib(Column::ROOM_NAME), LobbyItemName::NameRole).toString();
    const bool has_pw =
        m->data(sib(Column::ROOM_NAME), LobbyItemName::PasswordRole).toBool();
    const QString host =
        m->data(sib(Column::HOST), LobbyItemHost::HostUsernameRole).toString();
    const int cur_p =
        m->data(sib(Column::MEMBER), LobbyItemMemberList::MemberListRole).toList().size();
    const int max_p =
        m->data(sib(Column::MEMBER), LobbyItemMemberList::MaxPlayerRole).toInt();

    const QString tt = QStringLiteral(
        "<div style='font-family:\"Segoe UI\",\"Inter\",sans-serif;'>"
        "<b style='font-size:11pt;'>%1</b><br/>"
        "<span style='color:#aaa;font-size:9pt;'>🎮 %2</span><br/>"
        "<span style='color:#aaa;font-size:9pt;'>👤 %3</span><br/>"
        "<span style='color:#aaa;font-size:9pt;'>👥 %4 / %5</span>%6"
        "</div>")
        .arg(room_name.toHtmlEscaped(),
             game_name.isEmpty() ? tr("Any Game") : game_name.toHtmlEscaped(),
             host.isEmpty()      ? tr("Unknown")  : host.toHtmlEscaped())
        .arg(cur_p).arg(max_p)
        .arg(has_pw
            ? QStringLiteral("<br/><span style='color:#f90;'>🔒 Password required</span>")
            : QString{});

    QToolTip::showText(event->globalPos(), tt, view);
    return true;
}

// ============================================================
// AdvanceAnimations  — called by timer every 40 ms
// ============================================================

void LobbyCardDelegate::AdvanceAnimations() {
    if (!tree_view) return;
    bool dirty = false;

    // --- Hover glow ---
    const QPoint mp = tree_view->viewport()->mapFromGlobal(QCursor::pos());
    const QModelIndex hov_raw = tree_view->indexAt(mp);
    const QPersistentModelIndex hov_key =
        (hov_raw.isValid() && !hov_raw.parent().isValid())
            ? RowKey(hov_raw) : QPersistentModelIndex{};

    if (hov_key.isValid() && !hover_prog.contains(hov_key)) {
        hover_prog[hov_key] = 0.0;
    }

    for (auto it = hover_prog.begin(); it != hover_prog.end(); ) {
        const bool hov = (it.key() == hov_key);
        qreal& p = it.value();
        p = hov ? std::min(p + 0.10, 1.0)
                : std::max(p - 0.10, 0.0);
        dirty = true;
        it = (p <= 0.0 && !hov) ? hover_prog.erase(it) : std::next(it);
    }

    // --- Click pulse ---
    if (click_prog > 0.0) {
        click_prog = std::max(0.0, click_prog - 0.035);
        dirty = true;
    }

    // --- Marquee ---
    for (auto it = scroll_offset.begin(); it != scroll_offset.end(); ++it) {
        if (!it.key().isValid()) continue;
        auto& pause = scroll_pause_ticks[it.key()];
        if (pause > 0) { --pause; dirty = true; continue; }
        it.value() += kScrollSpeed;
        dirty = true;
    }

    if (dirty) tree_view->viewport()->update();
}

// ============================================================
// OnRowClicked
// ============================================================

void LobbyCardDelegate::OnRowClicked(const QModelIndex& idx) {
    if (!idx.isValid() || idx.parent().isValid()) return;
    click_idx  = RowKey(idx);
    click_prog = 1.0;
    if (tree_view) tree_view->viewport()->update();
}

// ============================================================
// SetAnimateNames  — toggled by "Scroll Names" checkbox
// ============================================================

void LobbyCardDelegate::SetAnimateNames(bool animate) {
    animate_names = animate;
    if (!animate) {
        // Clear scroll state so static text renders immediately
        scroll_offset.clear();
        scroll_pause_ticks.clear();
    }
    if (tree_view) tree_view->viewport()->update();
}


void LobbyCardDelegate::PaintBackground(QPainter* painter,
                                         const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const {
    // Extend to full viewport width so the unified card covers all columns
    QRect full_rect = option.rect;
    if (tree_view) {
        full_rect.setLeft(2);
        full_rect.setRight(tree_view->viewport()->width() - 2);
    }
    full_rect.adjust(0, kCardMarginV, 0, -kCardMarginV);

    const QPersistentModelIndex key = RowKey(index);
    const qreal hov  = hover_prog.value(key, 0.0);
    const bool  sel  = option.state & QStyle::State_Selected;

    // Hover glow behind card
    if (hov > 0.01) DrawHoverGlow(painter, full_rect, hov);

    // Card fill
    QColor bg = CardBg();
    if (hov) bg = bg.lighter(100 + (int)(hov * 16));

    QPainterPath path;
    path.addRoundedRect(full_rect, kCardRadius, kCardRadius);
    painter->fillPath(path, bg);

    // Selected accent stripe
    if (sel) {
        QPainterPath stripe;
        stripe.addRoundedRect(QRect(full_rect.left(), full_rect.top() + 6,
                                    3, full_rect.height() - 12), 2, 2);
        painter->fillPath(stripe, AccentColor());
    }

    // Pulse border (on click)
    if (click_idx.isValid() && click_prog > 0.0 && key == click_idx) {
        DrawPulseBorder(painter, full_rect, click_prog);
    }
}

// ============================================================
// PaintIcon  — 60×60 icon centred in 128 px column
// ============================================================

void LobbyCardDelegate::PaintIcon(QPainter* painter,
                                   const QRect& cell_rect,
                                   const QModelIndex& index) const {
    QPixmap icon = index.data(LobbyItemGame::GameIconRole).value<QPixmap>();
    if (icon.isNull()) return;

    const int icon_x = cell_rect.left() + (cell_rect.width()  - kIconSize) / 2;
    const int icon_y = cell_rect.top()  + (cell_rect.height() - kIconSize) / 2
                       + kCardMarginV;
    DrawRoundedIcon(painter, QRect(icon_x, icon_y, kIconSize, kIconSize), icon);
}

// ============================================================
// PaintRoomName  — always-on marquee in the stretch column
// ============================================================

void LobbyCardDelegate::PaintRoomName(QPainter* painter,
                                       const QRect& cell_rect,
                                       const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
    const QAbstractItemModel* m = index.model();
    const QString room_name =
        m->data(index, LobbyItemName::NameRole).toString();
    const bool has_pw =
        m->data(index, LobbyItemName::PasswordRole).toBool();
    const QString game_name =
        m->data(index.sibling(index.row(), Column::GAME_NAME),
                LobbyItemGame::GameNameRole).toString();

    // Name badge (upper portion of cell)
    const int pad_v    = kCardMarginV + 6;
    const int half_h   = (cell_rect.height() - kCardMarginV * 2) / 2;
    QRect name_rect(cell_rect.left() + 6,
                    cell_rect.top()  + pad_v,
                    cell_rect.width() - 12,
                    half_h - 2);

    const QString display = has_pw
        ? QStringLiteral("🔒 %1").arg(room_name)
        : room_name;

    QFont name_font = option.font;
    name_font.setBold(true);
    name_font.setPointSize(std::max(name_font.pointSize(), 9));

    // ---- Marquee / static ----
    const QPersistentModelIndex key = RowKey(index);
    if (!animate_names) {
        // Static mode: just draw with elide
        painter->save();
        painter->setFont(name_font);
        painter->setPen(FgColor());
        painter->drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          QFontMetrics(name_font).elidedText(display, Qt::ElideRight,
                                                              name_rect.width()));
        painter->restore();
    } else {
        // Animated marquee - two instances separated by " ⟫ " for a clean loop
        if (!scroll_offset.contains(key)) {
            scroll_offset[key]       = 0;
            scroll_pause_ticks[key]  = kScrollPause;
        }

        const QString separator  = QStringLiteral("   ⟫   ");
        const QString full_text  = display + separator;
        const QFontMetrics fm(name_font);
        const int unit_w = fm.horizontalAdvance(full_text); // one full cycle width

        int& offset = scroll_offset[key];
        if (offset >= unit_w) {
            offset = 0;
            scroll_pause_ticks[key] = kScrollPause;
        }

        painter->save();
        painter->setClipRect(name_rect);
        painter->setFont(name_font);
        painter->setPen(FgColor());

        // Draw as many instances as needed to fill the rect seamlessly
        int draw_x = name_rect.left() - offset;
        while (draw_x < name_rect.right() + unit_w) {
            QRect seg(draw_x, name_rect.top(), unit_w, name_rect.height());
            painter->drawText(seg, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              full_text);
            draw_x += unit_w;
        }

        // Multi-side fade (short distance)
        {
            const int fw = 20;
            QColor bg = CardBg();
            
            // Left-edge fade
            QLinearGradient left_fade(name_rect.left(), 0, name_rect.left() + fw, 0);
            left_fade.setColorAt(0.0, bg);
            left_fade.setColorAt(1.0, QColor(bg.red(), bg.green(), bg.blue(), 0));
            painter->fillRect(QRect(name_rect.left(), name_rect.top(),
                                    fw, name_rect.height()), left_fade);

            // Right-edge fade
            QLinearGradient right_fade(name_rect.right() - fw, 0, name_rect.right(), 0);
            right_fade.setColorAt(0.0, QColor(bg.red(), bg.green(), bg.blue(), 0));
            right_fade.setColorAt(1.0, bg);
            painter->fillRect(QRect(name_rect.right() - fw, name_rect.top(),
                                    fw + 1, name_rect.height()), right_fade);
        }
        painter->restore();
    }

    // Sub-line: game name
    const int sub_y = cell_rect.top() + pad_v + half_h;
    const int sub_h = cell_rect.bottom() - sub_y - kCardMarginV - 4;
    QRect sub_rect(cell_rect.left() + 6, sub_y, cell_rect.width() - 12, sub_h);

    QFont sub_font = option.font;
    sub_font.setPointSize(std::max(sub_font.pointSize() - 1, 7));
    painter->setFont(sub_font);
    painter->setPen(DimColor());
    painter->drawText(sub_rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                      QFontMetrics(sub_font).elidedText(
                          game_name.isEmpty() ? tr("Any Game") : game_name,
                          Qt::ElideRight, sub_rect.width()));
}

// ============================================================
// PaintBadge  — player count pill centred in 133 px column
// ============================================================

void LobbyCardDelegate::PaintBadge(QPainter* painter,
                                    const QRect& cell_rect,
                                    const QModelIndex& index) const {
    const QAbstractItemModel* m = index.model();
    const int cur = m->data(index, LobbyItemMemberList::MemberListRole).toList().size();
    const int max = m->data(index, LobbyItemMemberList::MaxPlayerRole).toInt();

    const int bw = 88, bh = 24;
    QRect badge(cell_rect.left()  + (cell_rect.width()  - bw) / 2,
                cell_rect.top()   + (cell_rect.height() - bh) / 2 + kCardMarginV,
                bw, bh);
    DrawPlayerBadge(painter, badge, cur, max);
}

// ============================================================
// PaintChildCard  — member row spans all visible columns
// ============================================================

void LobbyCardDelegate::PaintChildCard(QPainter* painter,
                                        const QRect& full_rect,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const {
    // Indent to sit under the room name area
    QRect r = full_rect.adjusted(128 + 8, 0, 0, 0);

    QPainterPath path;
    path.addRoundedRect(r, 4, 4);
    painter->fillPath(path, ChildBg());

    const QString text = index.data(Qt::DisplayRole).toString();
    QFont f = option.font;
    f.setPointSize(std::max(f.pointSize() - 1, 7));
    painter->setFont(f);
    painter->setPen(DimColor());
    painter->drawText(r.adjusted(10, 0, -6, 0),
                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                      QFontMetrics(f).elidedText(text, Qt::ElideRight, r.width() - 16));
}

// ============================================================
// Draw primitives
// ============================================================

void LobbyCardDelegate::DrawRoundedIcon(QPainter* painter,
                                         const QRect& r,
                                         const QPixmap& px) const {
    if (px.isNull()) return;
    painter->save();
    QPainterPath clip;
    clip.addRoundedRect(r, 6, 6);
    painter->setClipPath(clip);
    painter->drawPixmap(r, px.scaled(r.size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
    painter->restore();
    // Subtle rim
    painter->save();
    QPainterPath rim; rim.addRoundedRect(r, 6, 6);
    painter->setPen(QPen(QColor(255, 255, 255, 22), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(rim);
    painter->restore();
}

void LobbyCardDelegate::DrawPlayerBadge(QPainter* painter,
                                         const QRect& badge,
                                         int cur, int max) const {
    const QColor col = PlayerColor(cur, max);
    painter->save();

    QPainterPath pill;
    pill.addRoundedRect(badge, badge.height() / 2.0, badge.height() / 2.0);
    QColor fill = col; fill.setAlpha(35);
    painter->fillPath(pill, fill);
    painter->setPen(QPen(col, 1.2));
    painter->drawPath(pill);

    QFont f = QApplication::font();
    f.setPointSize(std::max(f.pointSize() - 1, 7));
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(col);
    const QString label = max > 0
        ? QStringLiteral("%1 / %2").arg(cur).arg(max)
        : QString::number(cur);
    painter->drawText(badge, Qt::AlignCenter | Qt::TextSingleLine, label);
    painter->restore();
}

void LobbyCardDelegate::DrawPulseBorder(QPainter* painter,
                                         const QRect& rect,
                                         qreal progress) const {
    QColor accent = AccentColor();
    painter->save();
    painter->setBrush(Qt::NoBrush);
    for (int ring = 0; ring < 3; ++ring) {
        qreal rp = progress - ring * 0.18;
        if (rp <= 0.0) break;
        const int expand = ring * 3 + (int)((1.0 - rp) * 6);
        QColor c = accent;
        c.setAlpha((int)(rp * 180 / (ring + 1)));
        QPainterPath p;
        p.addRoundedRect(rect.adjusted(-expand, -expand, expand, expand),
                         kCardRadius + expand, kCardRadius + expand);
        painter->setPen(QPen(c, 1.5 - ring * 0.4));
        painter->drawPath(p);
    }
    painter->restore();
}

void LobbyCardDelegate::DrawHoverGlow(QPainter* painter,
                                       const QRect& rect,
                                       qreal progress) const {
    QColor accent = AccentColor();
    painter->save();
    for (int i = 4; i >= 1; --i) {
        QColor c = accent;
        c.setAlpha((int)(progress * 18 * (5 - i)));
        QPainterPath p;
        p.addRoundedRect(rect.adjusted(-i*2, -i*2, i*2, i*2),
                         kCardRadius + i*2, kCardRadius + i*2);
        painter->fillPath(p, c);
    }
    painter->restore();
}

// ============================================================
// Colour helpers — charcoal grey, no blue tint
// ============================================================

QColor LobbyCardDelegate::CardBg() const {
    return UISettings::IsDarkTheme()
        ? QColor(36, 36, 40)
        : QColor(240, 240, 244);
}

QColor LobbyCardDelegate::ChildBg() const {
    return UISettings::IsDarkTheme()
        ? QColor(30, 30, 33, 200)
        : QColor(225, 225, 228, 200);
}

QColor LobbyCardDelegate::FgColor() const {
    return UISettings::IsDarkTheme() ? QColor(230, 230, 234) : QColor(22, 22, 28);
}

QColor LobbyCardDelegate::DimColor() const {
    return UISettings::IsDarkTheme() ? QColor(145, 145, 155) : QColor(100, 100, 112);
}

QColor LobbyCardDelegate::AccentColor() const {
    const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    if (QColor(hex).isValid()) {
        return QColor(hex);
    }
    const QColor pa = QApplication::palette().color(QPalette::Highlight);
    return (pa.isValid() && pa != Qt::black) ? pa : QColor(100, 149, 237);
}

QColor LobbyCardDelegate::PlayerColor(int cur, int max) const {
    if (max <= 0 || cur <= 0) return QColor(120, 120, 120); // grey — empty / unknown
    const double ratio = static_cast<double>(cur) / max;
    if (ratio >= 1.0)    return QColor(220,  60,  50); // red   — 100 % full
    if (ratio >= 0.90)   return QColor(230, 100,  25); // deep amber — 90-100 %
    if (ratio >= 0.60)   return QColor(220, 170,  30); // amber — 60-90 %
    return                      QColor( 50, 195,  85); // green — 0-60 %
}
