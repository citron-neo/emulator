// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeView>
#include <QHelpEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>

#include "citron/game_list.h"
#include "citron/game_list_delegate.h"
#include "citron/game_list_p.h"
#include "citron/uisettings.h"

/**
 * OnyxTooltip is a custom tooltip widget designed for the "Grey Onyx" aesthetic.
 * It enforces total opacity and provides high-end styling for HTML content.
 */
class OnyxTooltip : public QWidget {
    Q_OBJECT
public:
    explicit OnyxTooltip(QWidget* parent = nullptr) : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_StyledBackground);
        
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        
        m_label = new QLabel(this);
        m_label->setTextFormat(Qt::RichText);
        m_label->setWordWrap(true);
        m_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        layout->addWidget(m_label);

        setStyleSheet(QStringLiteral(
            "QWidget { background-color: #24242a; border: 1px solid #32323a; border-radius: 8px; }"
            "QLabel { color: #ffffff; background: transparent; border: none; font-family: 'Outfit', 'Inter', sans-serif; }"
        ));
    }

    static void showText(const QPoint& pos, const QString& text, QWidget* w) {
        static OnyxTooltip* instance = nullptr;
        if (!instance) {
            instance = new OnyxTooltip();
        }
        
        instance->m_label->setText(text);
        instance->adjustSize();
        
        QScreen* screen = QGuiApplication::screenAt(pos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        QRect screenGeom = screen->availableGeometry();
        
        QPoint showPos = pos + QPoint(10, 10);
        if (showPos.x() + instance->width() > screenGeom.right()) {
            showPos.setX(pos.x() - instance->width() - 10);
        }
        if (showPos.y() + instance->height() > screenGeom.bottom()) {
            showPos.setY(pos.y() - instance->height() - 10);
        }
        
        instance->move(showPos);
        instance->show();
        QTimer::singleShot(5000, instance, &QWidget::hide);
    }

    static void hideTooltip() {
        // Implementation simplified as static instance persists
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0x24, 0x24, 0x2a));
        painter.setPen(QColor(0x32, 0x32, 0x3a));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    }

private:
    QLabel* m_label;
};

GameListDelegate::GameListDelegate(QTreeView* view, QObject* parent)
    : QStyledItemDelegate(parent), tree_view(view) {
    animation_timer = new QTimer(this);
    connect(animation_timer, &QTimer::timeout, this, &GameListDelegate::AdvanceAnimations);
    animation_timer->start(40); // ~25 FPS
}

GameListDelegate::~GameListDelegate() = default;

int GameListDelegate::GetCardHeight() const {
    // Increase card height for more breathing room (from 12 to 22 extra px)
    return UISettings::values.game_icon_size.GetValue() + kCardMarginV * 2 + 22;
}

int GameListDelegate::GetIconSize() const {
    return UISettings::values.game_icon_size.GetValue();
}

QSize GameListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);

    // Check if it's a game row by checking if it has a parent folder
    const bool is_game_row = index.parent().isValid();

    if (is_game_row) {
        size.setHeight(GetCardHeight());
    } else {
        size.setHeight(36); // Clean height for folder headers
    }

    return size;
}

void GameListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    // Proven detection: children are games, top-level items are folders
    const bool is_game_row = index.parent().isValid();
    QRect rect = option.rect;

    // 1. Initial State Save
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);

    // ---- Bubble/Fade-in Animation Logic ----
    // Only apply animations if population is active or if specifically enabled
    qreal entry_val = 1.0;
    int vertical_offset = 0;

    if (enable_bubble_animations) {
        const QPersistentModelIndex key(index.siblingAtColumn(0));
        if (entry_animations.contains(key)) {
            entry_val = entry_animations[key];
        } else {
            // No active animation. Default to full visibility.
            entry_val = 1.0;
        }
        // Reduced offset (10px) and slide UP to prevent clipping artifacts
        vertical_offset = static_cast<int>(10.0 * (1.0 - entry_val));
    }

    painter->translate(0, vertical_offset);
    painter->setOpacity(entry_val * population_fade_global);

    if (is_game_row) {
        // --- Game Row Rendering ---
        // Every column paints its specific "slice" of the row-spanning card background.
        // This naturally avoids both the "grey block" and global clipping leaks.
        painter->save();
        PaintBackground(painter, option, index);
        painter->restore();

        // Specific Content Rendering
        painter->save();
        painter->setClipRect(rect); // Keep content strictly within its cell
        switch (index.column()) {
        case GameList::COLUMN_NAME:
            PaintGameInfo(painter, rect, option, index);
            break;
        case GameList::COLUMN_COMPATIBILITY:
            PaintCompatibility(painter, rect, index);
            break;
        case GameList::COLUMN_PLAY_TIME:
            PaintPlayTime(painter, rect, option, index);
            break;
        default:
            PaintDefault(painter, rect, option, index);
            break;
        }
        painter->restore();
    } else {
        // --- Category/Folder Header Rendering ---
        const bool is_dark = UISettings::IsDarkTheme();
        const bool is_selected = option.state & QStyle::State_Selected;

        // Background for header (span full width if in first column)
        if (index.column() == 0 && tree_view) {
            QRect header_rect = rect;
            header_rect.setLeft(0);
            header_rect.setRight(tree_view->viewport()->width());

            QColor header_bg = is_dark ? QColor(42, 42, 48) : QColor(235, 235, 240);
            if (is_selected)
                header_bg = AccentColor().darker(120);

            painter->setPen(Qt::NoPen);
            painter->setBrush(header_bg);
            painter->drawRect(header_rect);
        }

        // Content for first column only
        if (index.column() == 0) {
            QVariant decoration = index.data(Qt::DecorationRole);
            int text_offset = 8;
            if (decoration.isValid()) {
                QPixmap icon_pixmap;
                if (decoration.canConvert<QPixmap>()) {
                    icon_pixmap = decoration.value<QPixmap>();
                } else if (decoration.canConvert<QIcon>()) {
                    icon_pixmap = decoration.value<QIcon>().pixmap(24, 24);
                }

                if (!icon_pixmap.isNull()) {
                    QRect icon_rect(rect.left() + 8, rect.top() + (rect.height() - 20) / 2, 20, 20);
                    painter->drawPixmap(icon_rect, icon_pixmap.scaled(20, 20, Qt::KeepAspectRatio,
                                                                      Qt::SmoothTransformation));
                    text_offset = 36;
                }
            }

            painter->setFont(option.font);
            painter->setPen(TextColor());
            QFont f = painter->font();
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(rect.adjusted(text_offset, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              index.data(Qt::DisplayRole).toString());
        }
    }

    // 2. Final State Restore
    painter->restore();
}

void GameListDelegate::AdvanceAnimations() {
    if (!tree_view || !tree_view->isVisible())
        return;

    QList<QModelIndex> indices_to_update;

    // 1. Hover animations (row-spanning glows)
    auto it_hov = hover_states.begin();
    while (it_hov != hover_states.end()) {
        const QPersistentModelIndex& key = it_hov.key();
        if (!key.isValid() || key.column() != 0) {
            it_hov = hover_states.erase(it_hov);
            continue;
        }

        // We track selection per row (column 0)
        const bool is_selected =
            tree_view->selectionModel()->isRowSelected(key.row(), key.parent());
        if (is_selected) {
            if (!pulse_states.contains(key)) {
                pulse_states[key] = 0.0;
                pulse_direction[key] = true;
            }
        }

        indices_to_update.append(key);
        ++it_hov;
    }

    // 2. Selection Pulse (Breathing effect)
    auto it_pulse = pulse_states.begin();
    while (it_pulse != pulse_states.end()) {
        const QPersistentModelIndex& key = it_pulse.key();
        if (!key.isValid() || !tree_view->selectionModel()->isSelected(key)) {
            it_pulse = pulse_states.erase(it_pulse);
            pulse_direction.remove(key);
            continue;
        }

        qreal& val = it_pulse.value();
        bool& dir = pulse_direction[key];

        if (dir) {
            val += 0.05;
            if (val >= 1.0)
                dir = false;
        } else {
            val -= 0.05;
            if (val <= 0.0)
                dir = true;
        }

        indices_to_update.append(key);
        ++it_pulse;
    }

    // 4. Entry animations (Bubble/Fade-in)
    auto it_entry = entry_animations.begin();
    
    // 5. Global Refresh Spinner Animation
    refresh_angle -= 10.0; // Rotate 10 degrees per frame
    if (refresh_angle <= -360.0) refresh_angle += 360.0;
    
    // Trigger repaint for any item currently in 'Refreshing' state
    for (int i = 0; i < tree_view->model()->rowCount(); ++i) {
        QModelIndex folder = tree_view->model()->index(i, 0);
        for (int j = 0; j < tree_view->model()->rowCount(folder); ++j) {
            QModelIndex play_time_idx = tree_view->model()->index(j, GameList::COLUMN_PLAY_TIME, folder);
            if (play_time_idx.data(GameListItem::IsRefreshingRole).toBool()) {
                indices_to_update.append(play_time_idx);
            }
        }
    }
    while (it_entry != entry_animations.end()) {
        const QPersistentModelIndex& key = it_entry.key();
        if (!key.isValid()) {
            it_entry = entry_animations.erase(it_entry);
            continue;
        }

        qreal& val = it_entry.value();
        if (val < 1.0) {
            val += 0.08; // Smooth entry
            if (val >= 1.0)
                val = 1.0;
            indices_to_update.append(key);
            ++it_entry;
        } else {
            // Animation finished. Remove from map to reduce overhead.
            it_entry = entry_animations.erase(it_entry);
        }
    }

    // 5. Global population fade transition
    if (is_populating && population_fade_global > 0.6) {
        population_fade_global -= 0.02;
        if (population_fade_global < 0.6)
            population_fade_global = 0.6;
        tree_view->viewport()->update();
    } else if (!is_populating && population_fade_global < 1.0) {
        population_fade_global += 0.04;
        if (population_fade_global > 1.0)
            population_fade_global = 1.0;
        tree_view->viewport()->update();
    }

    // Perform granular row-spanning updates
    if (tree_view && tree_view->viewport()) {
        const int viewport_width = tree_view->viewport()->width();
        for (const auto& index : indices_to_update) {
            if (index.isValid()) {
                QRect row_rect = tree_view->visualRect(index);
                // Force the update for the *entire* row across all columns
                row_rect.setLeft(0);
                row_rect.setRight(viewport_width);
                tree_view->viewport()->update(row_rect);
            }
        }
    }
}

bool GameListDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
                                 const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::ToolTip && index.isValid()) {
        const QString text = index.data(Qt::ToolTipRole).toString();
        if (!text.isEmpty()) {
            OnyxTooltip::showText(event->globalPos(), text, view);
            return true;
        }
    }
    OnyxTooltip::hideTooltip();
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

void GameListDelegate::PaintBackground(QPainter* painter, const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
    const bool is_selected = option.state & QStyle::State_Selected;

    // The 'Card Rect' spans the entire row width
    QRect card_rect = option.rect;
    if (tree_view && tree_view->header()) {
        auto* header = tree_view->header();
        int total_width = 0;
        for (int i = 0; i < header->count(); ++i) {
            if (!header->isSectionHidden(i)) {
                total_width += header->sectionSize(i);
            }
        }
        card_rect.setLeft(2);
        card_rect.setRight(total_width - 4);
    }
    card_rect.adjust(0, kCardMarginV, 0, -kCardMarginV);

    // CRITICAL: We only paint the 'slice' of the card that belongs to this column.
    // This prevents subsequent columns (e.g. 'Size') from overdrawing Column 0's text.
    painter->save();

    // Expand clip for Column 0 to include the indentation/margin area
    QRect clip_rect = option.rect;
    if (index.column() == 0) {
        clip_rect.setLeft(0);
    }
    painter->setClipRect(clip_rect);

    const QPersistentModelIndex key(index.siblingAtColumn(0));
    // Ensure the row is tracked for animations (pulse & marquee)
    if (!hover_states.contains(key)) {
        hover_states[key] = 0.0;
    }

    qreal pulse = 0.0;
    if (is_selected && pulse_states.contains(key)) {
        pulse = pulse_states[key];
    }

    QColor bg = is_selected ? SelectionColor() : CardBg();
    QPainterPath path;
    path.addRoundedRect(card_rect, kCardRadius, kCardRadius);

    // 1. Fill card background slice
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawPath(path);

    // 2. Draw accent outline slice
    QColor border_color = is_selected ? AccentColor() : QColor(255, 255, 255, 20);
    if (!UISettings::IsDarkTheme() && !is_selected)
        border_color = QColor(0, 0, 0, 20);

    float pulse_width_extra = is_selected ? (0.2f + (float)pulse * 0.8f) : 0.0f;
    if (is_selected) {
        border_color.setAlphaF(static_cast<float>(0.3 + (pulse * 0.7)));
    }

    painter->setPen(QPen(border_color, 1.2f + pulse_width_extra));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);

    // 3. Selected Accent Stripe
    if (is_selected) {
        QRect stripe(card_rect.left() + 1, card_rect.top() + 12, 3, card_rect.height() - 24);
        painter->setPen(Qt::NoPen);
        painter->setBrush(AccentColor());
        painter->drawRoundedRect(stripe, 1.5, 1.5);
    }

    painter->restore();
}

void GameListDelegate::PaintGameInfo(QPainter* painter, const QRect& rect,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    painter->save();
    painter->setClipRect(rect);
    const QModelIndex master_index = index.siblingAtColumn(0);
    const int icon_size = GetIconSize();
    const int margin_h = 12;
    const int v_pad = (rect.height() - icon_size) / 2;
    QRect icon_rect(rect.left() + margin_h, rect.top() + v_pad, icon_size, icon_size);

    // 1. Icon Rendering (HighRes -> Decoration fallback)
    QPixmap pixmap = master_index.data(GameListItemPath::HighResIconRole).value<QPixmap>();
    if (pixmap.isNull()) {
        QVariant decoration = master_index.data(Qt::DecorationRole);
        if (decoration.canConvert<QPixmap>()) {
            pixmap = decoration.value<QPixmap>();
        } else if (decoration.canConvert<QIcon>()) {
            pixmap = decoration.value<QIcon>().pixmap(icon_size, icon_size);
        }
    }

    if (!pixmap.isNull()) {
        // True Greyscale logic during population
        if (is_populating) {
            QString path = master_index.data(GameListItemPath::FullPathRole).toString();
            if (!greyscale_icon_cache.contains(path)) {
                QImage img = pixmap.toImage().convertToFormat(QImage::Format_Grayscale8);
                greyscale_icon_cache[path] = QIcon(QPixmap::fromImage(img));
            }
            pixmap = greyscale_icon_cache[path].pixmap(icon_size, icon_size);
        }

        QPainterPath icon_path;
        icon_path.addRoundedRect(icon_rect, 6, 6);
        painter->save();
        painter->setClipPath(icon_path);
        painter->drawPixmap(icon_rect, pixmap.scaled(icon_size, icon_size, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
        painter->restore();

        painter->setPen(QPen(QColor(255, 255, 255, 30), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(icon_rect, 6, 6);
    }

    // 2. Metadata Extraction (Title & Program ID)
    QString title = master_index.data(GameListItemPath::TitleRole).toString();
    u64 program_id = master_index.data(GameListItemPath::ProgramIdRole).toULongLong();

    // Absolute fallback: If TitleRole is empty, parse DisplayRole
    if (title.isEmpty()) {
        QString full_display = master_index.data(Qt::DisplayRole).toString();
        QStringList parts = full_display.split(QLatin1Char('\n'));
        title = parts.value(0).trimmed();
        // If we still have no ID, try parsing it from the second line
        if (program_id == 0 && parts.size() > 1) {
            QString id_str = parts.value(1).trimmed().remove(QStringLiteral("    "));
            if (id_str.startsWith(QStringLiteral("0x"))) {
                program_id = id_str.toULongLong(nullptr, 16);
            }
        }
    }

    const QString id =
        program_id > 0 ? QStringLiteral("0x%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper()
                       : QString();

    // 3. Text Rendering (Safe Baseline-based Drawing)
    const int text_x = icon_rect.right() + 18;
    const QFontMetrics metrics = painter->fontMetrics();
    const int title_ascent = metrics.ascent();
    const int title_descent = metrics.descent();
    const int title_h = title_ascent + title_descent;

    // Subtext (ID) metrics
    QFont f_id = option.font;
    f_id.setPointSize(std::max(6, f_id.pointSize() - 2));
    const QFontMetrics id_metrics(f_id);
    const int id_h = id_metrics.ascent() + id_metrics.descent();

    const int spacing = 6; // More generous spacing for large fonts
    const int total_h = title_h + (id.isEmpty() ? 0 : id_h + spacing);
    const int start_y = rect.top() + (rect.height() - total_h) / 2;

    // Draw Title (Bold)
    painter->setPen(TextColor());
    QFont f_title = option.font;
    f_title.setBold(true);
    painter->setFont(f_title);

    // Explicitly draw at the title baseline
    const int title_baseline = start_y + title_ascent;
    QString elided_title = metrics.elidedText(title, Qt::ElideRight, rect.right() - text_x - 12);
    painter->drawText(text_x, title_baseline, elided_title);

    // Draw Subtext (Program ID)
    if (!id.isEmpty()) {
        painter->setPen(DimColor());
        painter->setFont(f_id);

        // Explicitly draw at the ID baseline
        const int id_baseline = title_baseline + title_descent + spacing + id_metrics.ascent();
        painter->drawText(text_x, id_baseline, id);
    }
    painter->restore();
}

void GameListDelegate::PaintCompatibility(QPainter* painter, const QRect& rect,
                                          const QModelIndex& index) const {
    const QString text = index.data(Qt::DisplayRole).toString();
    const QString status_str = index.data(GameListItemCompat::CompatNumberRole).toString();

    // In a sub-item, status_str should be populated
    if (status_str.isEmpty())
        return;

    const int bw = 84, bh = 22;
    int final_bw = std::min(bw, rect.width() - 8);
    if (final_bw < 30)
        return;

    QRect badge(rect.left() + (rect.width() - final_bw) / 2, rect.top() + (rect.height() - bh) / 2,
                final_bw, bh);

    QColor color;
    if (status_str == QStringLiteral("0"))
        color = QColor(92, 147, 237); // Perfect
    else if (status_str == QStringLiteral("1"))
        color = QColor(71, 211, 92); // Playable
    else if (status_str == QStringLiteral("2"))
        color = QColor(242, 214, 36); // Ingame
    else if (status_str == QStringLiteral("3"))
        color = QColor(242, 214, 36); // Ingame (fallback)
    else if (status_str == QStringLiteral("4"))
        color = QColor(255, 0, 0); // Intro/Menu
    else if (status_str == QStringLiteral("5"))
        color = QColor(130, 130, 130); // Won't Boot
    else
        color = QColor(140, 140, 150); // Not Tested

    QColor fill = color;
    fill.setAlpha(40);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(badge, bh / 2, bh / 2);

    painter->setPen(QPen(color, 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(badge.adjusted(1, 1, -1, -1), bh / 2, bh / 2);

    // Map status string back to human-readable text for the badge
    QString status_text;
    if (status_str == QStringLiteral("0"))
        status_text = tr("Perfect");
    else if (status_str == QStringLiteral("1"))
        status_text = tr("Playable");
    else if (status_str == QStringLiteral("2"))
        status_text = tr("Ingame");
    else if (status_str == QStringLiteral("3"))
        status_text = tr("Ingame");
    else if (status_str == QStringLiteral("4"))
        status_text = tr("Intro/Menu");
    else if (status_str == QStringLiteral("5"))
        status_text = tr("Won't Boot");
    else
        status_text = tr("Not Tested");

    painter->setPen(color);
    QFont f = painter->font();
    f.setBold(true);
    f.setPixelSize(11); // Fixed size to prevent clipping with scaling
    painter->setFont(f);

    painter->drawText(badge, Qt::AlignCenter, status_text);
}

void GameListDelegate::PaintDefault(QPainter* painter, const QRect& rect,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    QString text = index.data(Qt::DisplayRole).toString();
    if (text.isEmpty()) {
        text = index.data(Qt::EditRole).toString();
    }

    if (text.isEmpty())
        return;

    if (text == QStringLiteral("N/A") || text == QStringLiteral("0 minutes") ||
        text == QStringLiteral("0 m")) {
        painter->setPen(DimColor());
    } else {
        painter->setPen(TextColor());
    }

    painter->setFont(option.font);

    const int margin = 10;
    QRect content_rect = rect.adjusted(margin, 4, -margin, -4);

    // Check if we need scrolling (only for Add-ons or columns with likely multi-line text)
    const bool is_addons = index.column() == GameList::COLUMN_ADD_ONS;

    if (is_addons) {
        // Optimization: Use cache for string processing
        if (!addons_item_cache.contains(text)) {
            QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            addons_item_cache.insert(text, lines);

            // Limit cache size
            if (addons_item_cache.size() > 500)
                addons_item_cache.clear();
        }

        const QStringList& lines = addons_item_cache[text];

        // Increase line height for breathing room
        const int line_v_padding = 2;
        const int line_h = painter->fontMetrics().height() + line_v_padding;
        const int max_lines = content_rect.height() / line_h;
        const bool is_hovered = option.state & QStyle::State_MouseOver;

        const int total_h = lines.size() * line_h;

        if (lines.size() > 1 || total_h > content_rect.height()) {
            const QPersistentModelIndex key(index);

            if (is_hovered && total_h > content_rect.height()) {
                if (!vertical_scroll_offsets.contains(key)) {
                    vertical_scroll_offsets[key] = 0;
                    vertical_scroll_pause[key] = 30;
                }

                int& offset = vertical_scroll_offsets[key];
                int& pause = vertical_scroll_pause[key];
                const int full_max_offset = total_h - content_rect.height() + 10;

                if (pause > 0) {
                    pause--;
                } else if (offset < full_max_offset) {
                    offset++;
                } else {
                    offset = 0;
                    pause = 60;
                }

                painter->save();
                painter->setClipRect(content_rect);
                painter->translate(0, -offset);
                for (int i = 0; i < lines.size(); ++i) {
                    painter->drawText(QRect(content_rect.left(), content_rect.top() + (i * line_h),
                                            content_rect.width(), line_h),
                                      Qt::AlignVCenter | Qt::AlignLeft,
                                      painter->fontMetrics().elidedText(lines[i], Qt::ElideRight,
                                                                        content_rect.width()));
                }
                painter->restore();
            } else {
                // Decay scroll offset when not hovered
                if (vertical_scroll_offsets.contains(key)) {
                    vertical_scroll_offsets.remove(key);
                    vertical_scroll_pause.remove(key);
                }

                painter->save();
                painter->setClipRect(content_rect);
                const int block_h = std::min((int)lines.size(), max_lines) * line_h;
                const int block_top =
                    content_rect.top() + std::max(0, (content_rect.height() - block_h) / 2);

                for (int i = 0; i < std::min((int)lines.size(), max_lines); ++i) {
                    painter->drawText(QRect(content_rect.left(), block_top + (i * line_h),
                                            content_rect.width(), line_h),
                                      Qt::AlignVCenter | Qt::AlignLeft,
                                      painter->fontMetrics().elidedText(lines[i], Qt::ElideRight,
                                                                        content_rect.width()));
                }

                if (lines.size() > max_lines) {
                    painter->setPen(DimColor());
                    painter->drawText(content_rect, Qt::AlignBottom | Qt::AlignRight,
                                      QStringLiteral("..."));
                }
                painter->restore();
            }
            return;
        }
    }

    // Default static rendering
    painter->drawText(
        content_rect, Qt::AlignVCenter | Qt::AlignLeft,
        painter->fontMetrics().elidedText(text, Qt::ElideRight, content_rect.width()));
}

QColor GameListDelegate::CardBg() const {
    return UISettings::IsDarkTheme() ? QColor(36, 36, 42) : QColor(245, 245, 250);
}

QColor GameListDelegate::TextColor() const {
    return UISettings::IsDarkTheme() ? QColor(240, 240, 245) : QColor(30, 30, 35);
}

QColor GameListDelegate::DimColor() const {
    return UISettings::IsDarkTheme() ? QColor(150, 150, 160) : QColor(105, 105, 118);
}

QColor GameListDelegate::SelectionColor() const {
    // Subtle highlight ONLY (5-10% shift from card background)
    QColor base = CardBg();
    if (UISettings::IsDarkTheme()) {
        return base.lighter(108);
    } else {
        return base.darker(105);
    }
}

QColor GameListDelegate::AccentColor() const {
    const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    if (QColor(hex).isValid()) {
        return QColor(hex);
    }
    const QColor pa = QApplication::palette().color(QPalette::Highlight);
    return (pa.isValid() && pa != Qt::black) ? pa : QColor(100, 149, 237);
}

void GameListDelegate::SetPopulating(bool populating) {
    if (is_populating == populating)
        return;
    is_populating = populating;
    enable_bubble_animations = populating;

    if (!populating) {
        // Accelerate final fade-up when population ends
        if (population_fade_global < 0.95)
            population_fade_global = 0.95;
        // NOTE: We no longer ClearAnimations() here to allow in-progress fades to finish.
        greyscale_icon_cache.clear();
    }

    // Force a full update to transition the opacity
    if (tree_view && tree_view->viewport()) {
        tree_view->viewport()->update();
    }
}

void GameListDelegate::RegisterEntryAnimation(const QModelIndex& index) {
    if (!index.isValid() || !enable_bubble_animations)
        return;
    const QPersistentModelIndex key(index.siblingAtColumn(0));
    if (!entry_animations.contains(key)) {
        entry_animations[key] = is_populating ? 0.2 : 0.0;
    }
}

void GameListDelegate::ClearAnimations() {
    entry_animations.clear();
    if (tree_view && tree_view->viewport()) {
        tree_view->viewport()->update();
    }
}

void GameListDelegate::PaintPlayTime(QPainter* painter, const QRect& rect,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    const bool is_refreshing = index.data(GameListItem::IsRefreshingRole).toBool();

    if (is_refreshing) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Center the spinner in the cell
        const int size = 16;
        const int x = rect.left() + (rect.width() - size) / 2;
        const int y = rect.top() + (rect.height() - size) / 2;
        QRect spinner_rect(x, y, size, size);

        QPen pen(AccentColor(), 2);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);

        // Draw a partial arc that rotates based on the global refresh_angle
        painter->drawArc(spinner_rect, static_cast<int>(refresh_angle * 16), 270 * 16);

        painter->restore();
    } else {
        // Fallback to standard text rendering if not refreshing
        PaintDefault(painter, rect, option, index);
    }
}

#include "game_list_delegate.moc"
