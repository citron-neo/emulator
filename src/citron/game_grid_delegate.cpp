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

GameGridDelegate::GameGridDelegate(QListView* view, QObject* parent)
    : QStyledItemDelegate(parent), m_view(view) {
    m_animation_timer = new QTimer(this);
    connect(m_animation_timer, &QTimer::timeout, this, &GameGridDelegate::AdvanceAnimations);
    m_animation_timer->start(32);
    m_greyscale_icon_cache.setMaxCost(500);
}

GameGridDelegate::~GameGridDelegate() = default;

void GameGridDelegate::setGridMode(GridMode mode) {
    m_grid_mode = mode;
}

QSize GameGridDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    const int icon_size = UISettings::values.game_icon_size.GetValue();
    const float scale = static_cast<float>(icon_size) / 128.0f;
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
    PaintGridItem(painter, option, index);
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

void GameGridDelegate::PaintGridItem(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    const bool is_selected = option.state & QStyle::State_Selected;
    QRect rect = option.rect;
    const int icon_size = UISettings::values.game_icon_size.GetValue();
    const float scale = static_cast<float>(icon_size) / 128.0f;

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
        double time_t = m_pulse_tick * 0.032;
        double hover_y = std::sin(time_t* 3.0) * (4.0 * scale);
        double yaw_angle = std::sin(time_t* 2.5) * 20.0;
        double pitch_angle = std::cos(time_t* 1.5) * 10.0;

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

    QColor onyx(22, 22, 26);
    painter->setBrush(onyx);
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
        sf.setPointSizeF(11.0f * scale);
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
        QRectF mid_area(label_rect.left(), label_rect.top(), label_rect.width(),
                        bottom_bar.top() - label_rect.top());
        painter->drawPixmap(mid_area, pixmap, pixmap.rect());
    }

    QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    painter->setPen(Qt::white);
    QFont tf = option.font;
    tf.setBold(true);
    tf.setPointSizeF(8.5f * scale);
    painter->setFont(tf);

    QRectF text_rect = bottom_bar.adjusted(10 * scale, 0, -10 * scale, 0);
    QString elided = painter->fontMetrics().elidedText(title, Qt::ElideRight, text_rect.width());
    painter->drawText(text_rect, Qt::AlignCenter, elided);

    painter->restore();

    painter->restore();
}

QColor GameGridDelegate::CardBg() const {
    return QColor(22, 22, 26);
}
QColor GameGridDelegate::TextColor() const {
    return QColor(255, 255, 255);
}
QColor GameGridDelegate::DimColor() const {
    return QColor(120, 120, 130);
}
QColor GameGridDelegate::SelectionColor() const {
    return QColor(35, 35, 40);
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
