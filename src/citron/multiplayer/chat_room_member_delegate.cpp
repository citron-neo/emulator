#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include "multiplayer/chat_room_member_delegate.h"
#include "multiplayer/chat_room.h"
#include "uisettings.h"
#include <QTimer>
#include <QLinearGradient>

ChatRoomMemberDelegate::ChatRoomMemberDelegate(QObject* parent) : QStyledItemDelegate(parent), anim_timer(new QTimer(this)) {
    anim_timer->setInterval(80); // Slower, less distracting animation
    connect(anim_timer, &QTimer::timeout, this, &ChatRoomMemberDelegate::AdvanceAnimations);
    anim_timer->start();
}

void ChatRoomMemberDelegate::AdvanceAnimations() {
    anim_offset = (anim_offset + 1) % 100000;
    // We would need access to the view view->viewport()->update() to trigger paint.
    // For now we rely on the same overarching event loops as the Lobby
    if (auto* mw = qobject_cast<QWidget*>(parent())) {
        mw->update(); // forces repaint of the view
    }
}

void ChatRoomMemberDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    bool is_compact = index.data(Qt::UserRole + 7).toBool();
    bool is_hovered = option.state & QStyle::State_MouseOver;
    bool is_selected = option.state & QStyle::State_Selected;

    QRect rect = option.rect;

    const bool is_dark = UISettings::IsDarkTheme();
    QColor bg_color = is_dark ? QColor("#292929") : QColor("#F4F4F4");

    // Sleek border-only selection
    QRect safe_rect = rect.adjusted(4, 2, -4, -2);
    if (is_selected) {
        painter->setBrush(bg_color);
        QColor accent = qApp->palette().color(QPalette::Highlight);
        painter->setPen(QPen(accent, 2));
        painter->drawRoundedRect(safe_rect, 6, 6);
    } else if (is_hovered) {
        QColor hover_color = is_dark ? QColor("#353535") : QColor("#EBEBEB");
        painter->setBrush(hover_color);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(safe_rect, 6, 6);
    } else {
        painter->setBrush(bg_color);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(safe_rect, 6, 6);
    }

    enum PlayerListRole {
        NicknameRole = Qt::UserRole + 1,
        UsernameRole = Qt::UserRole + 2,
        AvatarUrlRole = Qt::UserRole + 3,
        GameNameRole = Qt::UserRole + 4,
        GameVersionRole = Qt::UserRole + 5,
        StatusDotRole = Qt::UserRole + 6,
    };

    // Extract Data natively from the model
    QString nickname = index.data(PlayerListRole::NicknameRole).toString();
    QString game = index.data(PlayerListRole::GameNameRole).toString();
    QString version = index.data(PlayerListRole::GameVersionRole).toString();
    QString dot_type = index.data(PlayerListRole::StatusDotRole).toString();
    QColor player_color = index.data(Qt::UserRole + 8).value<QColor>();

    // Draw Letter Avatar
    int avatar_size = is_compact ? 44 : 40;
    int avatar_x = is_compact ? rect.x() + (rect.width() - avatar_size) / 2 : rect.x() + 8;
    int avatar_y = is_compact ? rect.y() + 6 : rect.y() + (rect.height() - avatar_size) / 2;

    // Draw Fading Highlight Ring (if recently chatted)
    float opacity = index.data(Qt::UserRole + 9).toFloat();
    if (opacity > 0.0f) {
        QColor ring_color = qApp->palette().color(QPalette::Highlight);
        ring_color.setAlphaF(opacity);
        painter->setPen(QPen(ring_color, 4));
        painter->drawEllipse(avatar_x, avatar_y, avatar_size, avatar_size);
    } else {
        painter->setPen(QPen(QColor(255, 255, 255, 30), 1));
        painter->drawEllipse(avatar_x, avatar_y, avatar_size, avatar_size);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(player_color);
    painter->drawEllipse(avatar_x + 2, avatar_y + 2, avatar_size - 4, avatar_size - 4);

    QString initial = nickname.isEmpty() ? QStringLiteral("?") : nickname.left(1).toUpper();
    QFont avatar_font = painter->font();
    avatar_font.setBold(true);
    avatar_font.setPointSize(is_compact ? 18 : 16);
    painter->setFont(avatar_font);
    
    int brightness = (player_color.red() * 299 + player_color.green() * 587 + player_color.blue() * 114) / 1000;
    painter->setPen(brightness > 125 ? Qt::black : Qt::white);
    
    // Nudge the text drawing rectangle slightly to perfectly center the capital letter visually
    QRect avatar_rect(avatar_x + 2, avatar_y + 2, avatar_size - 4, avatar_size - 4);
    painter->drawText(avatar_rect, Qt::AlignCenter, initial);

    // Draw Status Dot
    QColor dot_color = (dot_type == QStringLiteral("🟢")) ? Qt::green :
                       (dot_type == QStringLiteral("🟡")) ? Qt::yellow : Qt::gray;
    
    int dot_x = avatar_x + avatar_size - 10;
    int dot_y = avatar_y + avatar_size - 10;
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg_color); // Cutout effect
    painter->drawEllipse(dot_x - 1, dot_y - 1, 12, 12);
    painter->setBrush(dot_color);
    painter->drawEllipse(dot_x, dot_y, 10, 10);

    // Draw Typographic Text Elements
    auto draw_marquee = [&](const QString& txt, const QRect& bounds, const QFont& font, const QColor& color) {
        QFontMetrics fm(font);
        int text_width = fm.horizontalAdvance(txt);
        painter->setFont(font);
        painter->setPen(color);

        const QString separator = QStringLiteral("   ⟫   ");
        const QString full_text = txt + separator;
        int unit_w = fm.horizontalAdvance(full_text);

        if (text_width <= bounds.width() + 10 || is_selected) {
            // Standard static draw
            painter->drawText(bounds, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(txt, Qt::ElideRight, bounds.width()));
        } else {
            // Animated Marquee - mirrored from Lobby
            painter->save();
            painter->setClipRect(bounds);
            int offset = anim_offset % unit_w;
            int draw_x = bounds.left() - offset;
            
            while (draw_x < bounds.right() + unit_w) {
                QRect seg(draw_x, bounds.top(), unit_w, bounds.height());
                painter->drawText(seg, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, full_text);
                draw_x += unit_w;
            }

            // Multi-side fade (short distance)
            const int fw = 20;
            QColor fade_bg = is_hovered ? (is_dark ? QColor("#353535") : QColor("#EBEBEB")) : bg_color;

            // Left-edge fade
            QLinearGradient left_fade(bounds.left(), 0, bounds.left() + fw, 0);
            left_fade.setColorAt(0.0, fade_bg);
            left_fade.setColorAt(1.0, QColor(fade_bg.red(), fade_bg.green(), fade_bg.blue(), 0));
            painter->fillRect(QRect(bounds.left(), bounds.top(), fw, bounds.height()), left_fade);

            // Right-edge fade
            QLinearGradient right_fade(bounds.right() - fw, 0, bounds.right(), 0);
            right_fade.setColorAt(0.0, QColor(fade_bg.red(), fade_bg.green(), fade_bg.blue(), 0));
            right_fade.setColorAt(1.0, fade_bg);
            
            painter->fillRect(QRect(bounds.right() - fw, bounds.top(), fw + 1, bounds.height()), right_fade);
            painter->restore();
        }
    };

    if (is_compact) {
        QFont text_font = painter->font();
        text_font.setPointSize(8);
        text_font.setBold(true);
        painter->setFont(text_font);
        
        QRect text_rect(rect.x() + 2, rect.y() + avatar_size + 6, rect.width() - 4, 15);
        QString elided_name = painter->fontMetrics().elidedText(nickname, Qt::ElideRight, rect.width() - 4);
        painter->setPen(is_dark ? Qt::white : Qt::black);
        painter->drawText(text_rect, Qt::AlignCenter, elided_name);

    } else {
        int text_x = avatar_x + avatar_size + 12;
        int max_text_width = rect.width() - text_x - 8;
        
        QFont bold_font = painter->font();
        bold_font.setBold(true);
        bold_font.setPointSize(10);
        QColor name_color = is_dark ? QColor("#EAEAEA") : QColor("#222222");
        
        QRect name_rect(text_x, rect.y() + 8, max_text_width, 18);
        draw_marquee(nickname, name_rect, bold_font, name_color);

        QFont game_font = painter->font();
        game_font.setBold(false);
        game_font.setPointSize(8);
        QColor game_color = is_dark ? QColor("#A0A0A0") : QColor("#666666");

        QString full_game = game;
        if (!version.isEmpty()) full_game += QStringLiteral(" (%1)").arg(version);
        if (full_game.isEmpty()) full_game = QStringLiteral("Not playing");

        QRect game_rect(text_x, rect.y() + 26, max_text_width, 18);
        draw_marquee(full_game, game_rect, game_font, game_color);
    }

    painter->restore();
}

QSize ChatRoomMemberDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    bool is_compact = index.data(Qt::UserRole + 7).toBool();
    if (is_compact) {
        return QSize(80, 75);
    }
    return QSize(180, 56);
}
