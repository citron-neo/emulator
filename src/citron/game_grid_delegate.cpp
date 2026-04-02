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

QSize GameGridDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int icon_size = UISettings::values.game_icon_size.GetValue();
    return QSize(icon_size + 40, icon_size + 85);
}

void GameGridDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    if (!index.isValid()) return;
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    PaintGridItem(painter, option, index);
    painter->restore();
}

void GameGridDelegate::AdvanceAnimations() {
    if (!m_view || !m_view->isVisible()) return;

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
            if (val >= 1.0) dir = false;
        } else {
            val -= 0.04;
            if (val <= 0.0) dir = true;
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
            if (val >= 1.0) val = 1.0;
            ++it_entry;
        } else {
            it_entry = m_entry_animations.erase(it_entry);
        }
    }

    if (m_is_populating && m_population_fade_global > 0.6) {
        m_population_fade_global -= 0.02;
    } else if (!m_is_populating && m_population_fade_global < 1.0) {
        m_population_fade_global += 0.03;
        if (m_population_fade_global > 1.0) m_population_fade_global = 1.0;
    }

    m_pulse_tick++;
    m_view->viewport()->update();
}

void GameGridDelegate::PaintGridItem(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    const bool is_selected = option.state & QStyle::State_Selected;
    QRect rect = option.rect;

    qreal entry_val = 1.0;
    const QPersistentModelIndex key(index);
    if (m_entry_animations.contains(key)) entry_val = m_entry_animations[key];
    
    qreal final_opacity = entry_val * m_population_fade_global;
    painter->setOpacity(final_opacity);

    QRect card_rect = rect.adjusted(12, 12, -12, -12);
    const int icon_size = UISettings::values.game_icon_size.GetValue();
    const int card_h = icon_size + 64; 
    card_rect.setHeight(card_h);

    if (is_selected) {
        QColor glow = AccentColor();
        glow.setAlphaF(0.2);
        painter->save();
        painter->setBrush(glow);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(card_rect.adjusted(-4, -4, 4, 4), 16, 16);
        painter->restore();
    }

    QColor onyx(22, 22, 26);
    painter->setPen(Qt::NoPen);
    painter->setBrush(onyx);
    painter->drawRoundedRect(card_rect, 14, 14);

    if (is_selected) {
        QColor border = AccentColor();
        qreal pulse = (m_pulse_states.contains(key)) ? m_pulse_states[key] : 0.0;
        painter->setPen(QPen(border, 3.5 + pulse * 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card_rect, 14, 14);
    }

    QRectF label_rect = card_rect.adjusted(6, 6, -6, -22);
    QPainterPath label_path;
    label_path.addRoundedRect(label_rect, 8, 8);

    painter->save();
    painter->setClipPath(label_path);
    
    QRectF bottom_bar(label_rect.left(), label_rect.bottom() - 28, label_rect.width(), 28);
    painter->fillRect(bottom_bar, QColor(10, 10, 12));

    QPixmap pixmap = index.data(GameListItemPath::HighResIconRole).value<QPixmap>();
    if (pixmap.isNull()) pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!pixmap.isNull()) {
        QRectF mid_area(label_rect.left(), label_rect.top(), label_rect.width(), bottom_bar.top() - label_rect.top());
        painter->drawPixmap(mid_area, pixmap, pixmap.rect());
    }

    QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    painter->setPen(Qt::white);
    QFont tf = option.font;
    tf.setBold(true);
    tf.setPointSizeF(8.5);
    painter->setFont(tf);

    QRectF text_rect = bottom_bar.adjusted(10, 0, -10, 0);
    QString elided = painter->fontMetrics().elidedText(title, Qt::ElideRight, text_rect.width());
    painter->drawText(text_rect, Qt::AlignCenter, elided);
    
    painter->restore();

    int pins_y = card_rect.bottom() - 10;
    int p_c = 6;
    int p_w = (card_rect.width() - 40) / p_c;
    painter->setOpacity(0.8 * final_opacity);
    for (int i = 0; i < p_c; ++i) {
        painter->fillRect(card_rect.left() + 20 + i * p_w, pins_y, p_w - 6, 6, QColor(40, 40, 45));
    }
}

QColor GameGridDelegate::CardBg() const { return QColor(22, 22, 26); }
QColor GameGridDelegate::TextColor() const { return QColor(255, 255, 255); }
QColor GameGridDelegate::DimColor() const { return QColor(120, 120, 130); }
QColor GameGridDelegate::SelectionColor() const { return QColor(35, 35, 40); }
QColor GameGridDelegate::AccentColor() const {
    const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    return QColor(hex).isValid() ? QColor(hex) : QColor(0, 150, 255);
}

void GameGridDelegate::SetPopulating(bool populating) { m_is_populating = populating; }
void GameGridDelegate::RegisterEntryAnimation(const QModelIndex& index) { if (index.isValid()) m_entry_animations[QPersistentModelIndex(index)] = 0.0; }
void GameGridDelegate::ClearAnimations() { m_entry_animations.clear(); m_pulse_states.clear(); }
