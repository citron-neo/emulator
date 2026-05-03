#include <cmath>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QListView>
#include <QModelIndex>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTransform>

#include "citron/game_grid_delegate.h"
#include "citron/game_list_p.h"
#include "citron/uisettings.h"
#include "citron/custom_metadata.h"

GameGridDelegate::GameGridDelegate(QListView* view, QObject* parent)
    : QStyledItemDelegate(parent), m_view(view) {
    m_animation_timer = new QTimer(this);
    connect(m_animation_timer, &QTimer::timeout, this, &GameGridDelegate::AdvanceAnimations);
    m_animation_timer->start(32);
    m_greyscale_icon_cache.setMaxCost(500);
    m_poster_cache.setMaxCost(100);
}

GameGridDelegate::~GameGridDelegate() = default;

void GameGridDelegate::setGridMode(GridMode mode) {
    m_grid_mode = mode;
}

QSize GameGridDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    const int icon_size = std::max(32, static_cast<int>(UISettings::values.game_icon_size.GetValue()));
    const float scale = static_cast<float>(icon_size) / 128.0f;

    if (m_grid_mode == GridMode::Poster) {
        // Vertical aspect ratio (~2:3)
        return QSize(icon_size + static_cast<int>(20 * scale),
                     static_cast<int>(icon_size * 1.5) + static_cast<int>(60 * scale));
    }

    return QSize(icon_size + static_cast<int>(40 * scale),
                 icon_size + static_cast<int>(104 * scale));
}

void GameGridDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    if (!index.isValid())
        return;
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);
    if (m_grid_mode == GridMode::Poster) {
        PaintPosterItem(painter, option, index);
    } else {
        PaintGridItem(painter, option, index);
    }
    painter->restore();
}

void GameGridDelegate::AdvanceAnimations() {
    if (!m_view || !m_view->isVisible())
        return;

    auto it_pulse = m_pulse_states.begin();
    while (it_pulse != m_pulse_states.end()) {
        const QPersistentModelIndex& key = it_pulse.key();
        if (!key.isValid() || !m_view->selectionModel()->isSelected(key)) {
            it_pulse = m_pulse_states.erase(it_pulse);
            m_pulse_direction.remove(key);
            continue;
        }
        qreal& val = it_pulse.value();
        bool& dir = m_pulse_direction[key];
        if (dir) {
            val += 0.04;
            if (val >= 1.0)
                dir = false;
        } else {
            val -= 0.04;
            if (val <= 0.0)
                dir = true;
        }
        ++it_pulse;
    }

    auto it_entry = m_entry_animations.begin();
    while (it_entry != m_entry_animations.end()) {
        const QPersistentModelIndex& key = it_entry.key();
        if (!key.isValid()) {
            it_entry = m_entry_animations.erase(it_entry);
            continue;
        }
        qreal& val = it_entry.value();
        if (val < 1.0) {
            val += 0.06;
            if (val >= 1.0)
                val = 1.0;
            ++it_entry;
        } else {
            it_entry = m_entry_animations.erase(it_entry);
        }
    }

    if (m_is_populating && m_population_fade_global > 0.6) {
        m_population_fade_global -= 0.02;
    } else if (!m_is_populating && m_population_fade_global < 1.0) {
        m_population_fade_global += 0.03;
        if (m_population_fade_global > 1.0)
            m_population_fade_global = 1.0;
    }

    m_pulse_tick++;
    m_view->viewport()->update();
}

void GameGridDelegate::PaintPosterItem(QPainter* painter, const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
    const bool is_selected = option.state & QStyle::State_Selected;
    const bool is_hovered = option.state & QStyle::State_MouseOver;
    QRect rect = option.rect;
    const int icon_size = std::max(32, static_cast<int>(UISettings::values.game_icon_size.GetValue()));
    const float raw_scale = static_cast<float>(icon_size) / 128.0f;
    const float scale = std::max(0.1f, raw_scale);

    const QPersistentModelIndex key(index);
    qreal entry_val = 1.0;
    if (m_entry_animations.contains(key))
        entry_val = m_entry_animations[key];

    qreal final_opacity = entry_val * m_population_fade_global;
    painter->setOpacity(final_opacity);

    // Poster dimensions (2:3 aspect ratio)
    const int card_w = icon_size + static_cast<int>(12 * scale);
    const int card_h = static_cast<int>(icon_size * 1.5) + static_cast<int>(40 * scale);
    const int cx = rect.x() + (rect.width() - card_w) / 2;
    QRect card_rect(cx, rect.y() + static_cast<int>(10 * scale), card_w, card_h);

    const int radius = static_cast<int>(12 * scale);

    painter->save();

    // Hover/Selection animation state
    qreal hover_progress = m_hover_states.value(key, 0.0);
    const qreal step = 0.08; // Slightly slower, smoother transition
    if (is_hovered && hover_progress < 1.0) {
        hover_progress = std::min(1.0, hover_progress + step);
        m_hover_states[key] = hover_progress;
    } else if (!is_hovered && hover_progress > 0.0) {
        hover_progress = std::max(0.0, hover_progress - step);
        m_hover_states[key] = hover_progress;
    }

    if (is_selected || is_hovered) {
        painter->translate(card_rect.center());
        // Use a slight bounce effect for scale if hovered
        qreal scale_factor = 1.0 + (0.04 * hover_progress);
        if (is_selected) scale_factor = std::max(scale_factor, 1.05);
        painter->scale(scale_factor, scale_factor);
        painter->translate(-card_rect.center());
 
        // Outer Glow / Selection Ring
        QColor glow = AccentColor();
        glow.setAlphaF(0.3f * (is_selected ? 1.0f : hover_progress));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(glow, 2.0 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawRoundedRect(card_rect.adjusted(-1, -1, 1, 1), radius + 1, radius + 1);
    }

    // Main Card Background
    painter->setBrush(CardBg());
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(card_rect, radius, radius);

    // Image Clipping Path
    QPainterPath card_path;
    card_path.addRoundedRect(card_rect, radius, radius);
    painter->setClipPath(card_path);

    // Draw Poster / Icon
    u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    QPixmap* cached_pixmap = m_poster_cache.object(program_id);
    QPixmap pixmap;

    if (cached_pixmap) {
        pixmap = *cached_pixmap;
    } else {
        auto poster_path = Citron::CustomMetadata::GetInstance().GetCustomPosterPath(program_id);
        if (poster_path && !poster_path->empty()) {
            pixmap = QPixmap(QString::fromStdString(*poster_path));
        }

        if (pixmap.isNull()) {
            // Fallback to high-res icon if poster is missing
            pixmap = index.data(Qt::DecorationRole).value<QIcon>().pixmap(512, 512);
        }

        if (!pixmap.isNull()) {
            // Pre-scale the pixmap for the cache to save future CPU cycles
            pixmap = pixmap.scaled(card_rect.size(), Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
            m_poster_cache.insert(program_id, new QPixmap(pixmap));
        }
    }

    if (!pixmap.isNull()) {
        painter->drawPixmap(card_rect, pixmap);
    }

    // Gradient overlay for text readability
    QLinearGradient grad(card_rect.bottomLeft(), card_rect.center());
    grad.setColorAt(0, QColor(0, 0, 0, 180));
    grad.setColorAt(0.5, QColor(0, 0, 0, 0));
    painter->fillRect(card_rect, grad);

    // Metadata UI (Title, Glass Bar) - Only visible on hover/selection
    qreal metadata_opacity = std::max(is_selected ? 1.0 : 0.0, hover_progress);
    if (metadata_opacity > 0.0) {
        painter->save();
        painter->setOpacity(metadata_opacity * final_opacity);

        // Glassmorphism Bottom Bar
        qreal bar_h = 42.0f * scale;
        // Extend slightly at the bottom to prevent subpixel gaps
        QRectF bottom_bar(card_rect.left(), card_rect.bottom() - bar_h, card_rect.width(), bar_h + 2.0);
        
        QLinearGradient bar_grad(bottom_bar.topLeft(), bottom_bar.bottomLeft());
        bar_grad.setColorAt(0, QColor(25, 25, 30, 220));
        bar_grad.setColorAt(1, QColor(15, 15, 20, 240));
        
        painter->setBrush(bar_grad);
        painter->setPen(Qt::NoPen);
        painter->drawRect(bottom_bar);

        // Highlight line at the top of the glass bar
        painter->setPen(QPen(QColor(255, 255, 255, 40), 1.0));
        painter->drawLine(bottom_bar.topLeft(), bottom_bar.topRight());

        // Title Marquee
        QString title = index.data(Qt::DisplayRole).toString();
        painter->setPen(TextColor());
        QFont font = painter->font();
        font.setPointSizeF(std::max(8.0f, 10.5f * scale));
        font.setBold(true);
        painter->setFont(font);

        QRectF text_rect = bottom_bar.adjusted(10 * scale, 0, -10 * scale, 0);
        int text_width = painter->fontMetrics().horizontalAdvance(title);
        int available_w = static_cast<int>(text_rect.width());

        if (text_width > available_w) {
            // Marquee Animation Logic
            int scroll_range = text_width - available_w + static_cast<int>(20 * scale);
            int speed = 1; // 1 pixel per frame (~30fps)
            int pause_ticks = 45; // Pause at ends for ~1.5s
            int total_cycle = (scroll_range / speed) * 2 + pause_ticks * 2;
            int cycle_pos = m_pulse_tick % total_cycle;

            int offset = 0;
            if (cycle_pos < pause_ticks) {
                offset = 0;
            } else if (cycle_pos < pause_ticks + (scroll_range / speed)) {
                offset = (cycle_pos - pause_ticks) * speed;
            } else if (cycle_pos < pause_ticks * 2 + (scroll_range / speed)) {
                offset = scroll_range;
            } else {
                offset = scroll_range - (cycle_pos - (pause_ticks * 2 + (scroll_range / speed))) * speed;
            }

            painter->save();
            painter->setClipRect(text_rect);
            painter->drawText(text_rect.adjusted(-offset, 0, text_width, 0), 
                              Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
            painter->restore();
        } else {
            painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
        }

        painter->restore();
    }

    if (is_selected) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(AccentColor(), 3.0f * scale));
        painter->drawRoundedRect(card_rect, radius, radius);
    }

    painter->restore();
}

void GameGridDelegate::PaintGridItem(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    const bool is_selected = option.state & QStyle::State_Selected;
    QRect rect = option.rect;
    const int icon_size = std::max(32, static_cast<int>(UISettings::values.game_icon_size.GetValue()));
    const float raw_scale = static_cast<float>(icon_size) / 128.0f;
    const float scale = std::max(0.1f, raw_scale);

    qreal entry_val = 1.0;
    const QPersistentModelIndex key(index);
    if (m_entry_animations.contains(key))
        entry_val = m_entry_animations[key];

    qreal final_opacity = entry_val * m_population_fade_global;
    painter->setOpacity(final_opacity);

    const int card_w = icon_size + static_cast<int>(16 * scale);
    const int card_h = icon_size + static_cast<int>(64 * scale);
    const int cx = rect.x() + (rect.width() - card_w) / 2;
    QRect card_rect(cx, rect.y() + static_cast<int>(12 * scale), card_w, card_h);

    const int radius = static_cast<int>(14 * scale);

    painter->save();
    if (is_selected) {
        double pulse_t = m_pulse_tick * 0.032;
        double hover_y = std::sin(pulse_t* 3.0) * (4.0 * scale);
        double yaw_angle = std::sin(pulse_t* 2.5) * 20.0;
        double pitch_angle = std::cos(pulse_t* 1.5) * 10.0;

        painter->translate(rect.center());

        QTransform transform;
        transform.scale(1.04, 1.04);
        transform.translate(0, hover_y);

        QTransform rot;
        rot.rotate(yaw_angle, Qt::YAxis);
        rot.rotate(pitch_angle, Qt::XAxis);

        painter->setTransform(rot * transform, true);
        painter->translate(-rect.center());

        // --- 1. Selection Glow ---
        QColor glow = AccentColor();
        glow.setAlphaF(0.12f);
        painter->setBrush(glow);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(card_rect.adjusted(-4 * scale, -4 * scale, 4 * scale, 4 * scale),
                                 radius + 2, radius + 2);
    }

    QColor card_bg = CardBg();
    card_bg.setAlpha(255); // Force solid for cartridges
    painter->setBrush(card_bg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(card_rect, radius, radius);

    {
        painter->save();
        int pin_count = 12;
        qreal total_w = card_rect.width() * 0.85;
        qreal pin_w = (total_w / pin_count) * 0.4;
        qreal spacing = total_w / (pin_count - 1);
        qreal start_x = card_rect.left() + (card_rect.width() - total_w) / 2.0;

        for (int i = 0; i < pin_count; ++i) {
            QRectF pr(start_x + (i * spacing) - (pin_w / 2.0), card_rect.bottom() - (18 * scale),
                      pin_w, 14 * scale);

            QLinearGradient pg(pr.topLeft(), pr.bottomLeft());
            pg.setColorAt(0, QColor(10, 10, 12));
            pg.setColorAt(0.35, QColor(220, 200, 120)); // Bright Metallic Gold
            pg.setColorAt(0.65, QColor(160, 140, 70));
            pg.setColorAt(1, QColor(25, 25, 30));

            painter->setBrush(pg);
            painter->setPen(QPen(QColor(0, 0, 0, 180), 0.3 * scale));
            painter->drawRoundedRect(pr, 1 * scale, 1 * scale);

            painter->setPen(QPen(QColor(255, 255, 255, 50), 0.5 * scale));
            painter->drawLine(pr.left() + pr.width() * 0.2, pr.top() + pr.height() * 0.2,
                              pr.left() + pr.width() * 0.2, pr.top() + pr.height() * 0.5);
        }
        painter->restore();
    }

    if (is_selected) {
        QColor border = AccentColor();
        qreal pulse = (m_pulse_states.contains(key)) ? m_pulse_states[key] : 0.0;
        painter->setPen(QPen(border, (3.5f + pulse * 1.5f) * scale));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card_rect, radius, radius);
    }

    QRectF label_rect = card_rect.adjusted(6 * scale, 6 * scale, -6 * scale, -22 * scale);
    QPainterPath label_path;
    label_path.addRoundedRect(label_rect, radius - 6, radius - 6);

    // --- Favorites Indicator ---
    bool is_fav = (index.data(GameListItem::TypeRole).toInt() ==
                   static_cast<int>(GameListItemType::Favorites));
    if (is_fav) {
        painter->save();
        QColor fav_gold(255, 215, 0, 220); // Vibrant Gold
        painter->setPen(QPen(fav_gold, 1.2f * scale));
        painter->setBrush(QColor(40, 40, 45, 180));
        qreal star_size = 22.0f * scale;
        QRectF star_rect(card_rect.right() - (10.0f * scale) - star_size,
                         card_rect.top() + (10.0f * scale), star_size, star_size);
        painter->drawRoundedRect(star_rect, 6 * scale, 6 * scale);
        painter->setPen(fav_gold);
        QFont sf = painter->font();
        sf.setBold(true);
        sf.setPointSizeF(std::max(1.0f, 11.0f * scale));
        painter->setFont(sf);
        painter->drawText(star_rect.adjusted(0, -1 * scale, 0, 0), Qt::AlignCenter,
                          QStringLiteral("★"));
        painter->restore();
    }

    // Removed old vertical divider logic in favor of section headers

    painter->save();
    painter->setClipPath(label_path);

    qreal bar_h = 28.0f * scale;
    QRectF bottom_bar(label_rect.left(), label_rect.bottom() - bar_h, label_rect.width(), bar_h);
    painter->fillRect(bottom_bar, QColor(10, 10, 12));

    QPixmap pixmap = index.data(GameListItemPath::HighResIconRole).value<QPixmap>();
    if (pixmap.isNull())
        pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!pixmap.isNull()) {
        const qreal mid_h = bottom_bar.top() - label_rect.top();
        if (mid_h > 0) {
            QRectF mid_area(label_rect.left(), label_rect.top(), label_rect.width(), mid_h);
            painter->drawPixmap(mid_area, pixmap, pixmap.rect());
        }
    }

    QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    painter->setPen(TextColor());
    QFont tf = option.font;
    tf.setBold(true);
    tf.setPointSizeF(std::max(1.0f, 8.5f * scale));
    painter->setFont(tf);

    QRectF text_rect = bottom_bar.adjusted(10 * scale, 0, -10 * scale, 0);
    QString elided = painter->fontMetrics().elidedText(title, Qt::ElideRight, text_rect.width());
    painter->drawText(text_rect, Qt::AlignCenter, elided);

    painter->restore();

    painter->restore();
}

QColor GameGridDelegate::CardBg() const {
    const QString hex = QString::fromStdString(UISettings::values.custom_card_bg_color.GetValue());
    if (QColor(hex).isValid()) {
        return QColor(hex);
    }
    return QColor(22, 22, 26);
}
QColor GameGridDelegate::TextColor() const {
    const QString hex = QString::fromStdString(UISettings::values.custom_card_text_color.GetValue());
    if (QColor(hex).isValid()) {
        return QColor(hex);
    }
    return QColor(255, 255, 255);
}
QColor GameGridDelegate::DimColor() const {
    const QString hex =
        QString::fromStdString(UISettings::values.custom_card_dim_text_color.GetValue());
    if (QColor(hex).isValid()) {
        return QColor(hex);
    }
    return QColor(120, 120, 130);
}
QColor GameGridDelegate::SelectionColor() const {
    // Return transparent to remove the "weird fill-in" as requested by the user.
    return Qt::transparent;
}
QColor GameGridDelegate::AccentColor() const {
    const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    return QColor(hex).isValid() ? QColor(hex) : QColor(0, 150, 255);
}

void GameGridDelegate::SetPopulating(bool populating) {
    m_is_populating = populating;
}
void GameGridDelegate::RegisterEntryAnimation(const QModelIndex& index) {
    if (index.isValid())
        m_entry_animations[QPersistentModelIndex(index)] = 0.0;
}
void GameGridDelegate::ClearAnimations() {
    m_entry_animations.clear();
    m_pulse_states.clear();
}
void GameGridDelegate::ClearPosterCache() {
    m_poster_cache.clear();
}
