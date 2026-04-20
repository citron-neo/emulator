#include <cmath>
#include <QApplication>
#include <QEasingCurve>
#include <QPainterPath>
#include <QDateTime>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>

#include "citron/ui/game_carousel_view.h"
#include "citron/game_list_p.h"
#include "citron/uisettings.h"
#include "citron/theme.h"

CinematicCarousel::CinematicCarousel(QWidget* parent) : QWidget(parent) {
    m_snap_animation = new QPropertyAnimation(this, "focalIndex");
    m_snap_animation->setDuration(350);
    m_snap_animation->setEasingCurve(QEasingCurve::OutCubic);
    
    m_pulse_timer = new QTimer(this);
    connect(m_pulse_timer, &QTimer::timeout, this, [this]{ 
        m_pulse_tick++; 

        // Advance entry animations
        auto it = m_entry_animations.begin();
        while (it != m_entry_animations.end()) {
            if (!it.key().isValid()) {
                it = m_entry_animations.erase(it);
                continue;
            }
            if (it.value() < 1.0) {
                it.value() += 0.06;
                if (it.value() >= 1.0) it.value() = 1.0;
                ++it;
            } else {
                it = m_entry_animations.erase(it);
            }
        }
        update(); 
    });
    m_pulse_timer->start(32);
    
    m_scroll_timer = new QTimer(this);
    m_scroll_timer->setInterval(16);
    connect(m_scroll_timer, &QTimer::timeout, this, [this]{
        if (m_left_arrow_hover) setFocalIndex(m_focal_index - 0.1);
        else if (m_right_arrow_hover) setFocalIndex(m_focal_index + 0.1);
    });

    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setMinimumHeight(450);
    setContextMenuPolicy(Qt::CustomContextMenu);

    m_momentum_timer = new QTimer(this);
    m_momentum_timer->setInterval(16);
    connect(m_momentum_timer, &QTimer::timeout, this, [this] {
        if (std::abs(m_velocity) < 0.05) {
            m_momentum_timer->stop();
            startSnapAnimation(std::round(m_focal_index));
            return;
        }

        setFocalIndex(m_focal_index + m_velocity);
        m_velocity *= 0.92; // Friction factor
    });
}

QModelIndex CinematicCarousel::currentIndex() const {
    if (!m_model || m_model->rowCount() == 0) return QModelIndex();
    return m_model->index(std::round(m_focal_index), 0);
}

QModelIndex CinematicCarousel::indexAt(const QPoint& point) const {
    if (!m_model) return QModelIndex();
    const qreal vcx = width() / 2.0;
    const int is = UISettings::values.game_icon_size.GetValue();
    const qreal bs = is + 35.0;
    const qreal th = is * 2.0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const qreal d = i - m_focal_index;
        qreal x = vcx + (d * bs);
        const qreal dist = std::abs(d * bs);
        if (dist < th) x += d * (is / 2.0) * (1.0 - (dist / th));
        if (std::abs(point.x() - x) < (is * 0.7)) return m_model->index(i, 0);
    }
    return QModelIndex();
}

QRect CinematicCarousel::visualRect(const QModelIndex& index) const {
    if (!m_model || !index.isValid()) return QRect();
    const int i = index.row();
    const qreal vcx = width() / 2.0; const qreal vcy = height() / 2.0;
    const int is = UISettings::values.game_icon_size.GetValue();
    const qreal bs = is + 35.0; const qreal th = is * 2.0;
    const qreal d = i - m_focal_index; const qreal dist = std::abs(d * bs);
    qreal s = 1.0; qreal dx = 0.0;
    if (dist < th) { qreal f = 1.0 - (dist / th); s = 1.0 + (f * 0.40); dx = d * (is / 2.0) * f; }
    const qreal x = vcx + (d * bs) + dx; const int cs = is + 60;
    return QRect(x - (cs * s) / 2.0, vcy - (cs * s) / 2.0, cs * s, cs * s);
}

void CinematicCarousel::setModel(QAbstractItemModel* model) {
    if (m_model) {
        m_model->disconnect(this);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::rowsInserted, this, [this]() { update(); });
        connect(m_model, &QAbstractItemModel::modelReset, this, [this]() { update(); });
        connect(m_model, &QAbstractItemModel::dataChanged, this, [this]() { update(); });
    }
    if (m_model && m_model->rowCount() > 0) setFocalIndex(0.0);
    update();
}
void CinematicCarousel::setFocalIndex(qreal index) {
    if (!m_model || m_model->rowCount() == 0) m_focal_index = 0.0;
    else m_focal_index = std::max(0.0, std::min(static_cast<qreal>(m_model->rowCount() - 1), index));
    updateFocalItem(); update();
}

void CinematicCarousel::scrollTo(int index) { if (!m_model || index < 0 || index >= m_model->rowCount()) return; startSnapAnimation(index); }

void CinematicCarousel::RegisterEntryAnimation(const QModelIndex& index) {
    if (index.isValid()) {
        m_entry_animations[QPersistentModelIndex(index)] = 0.0;
    }
}

void CinematicCarousel::scrollToLetter(QChar letter) {
    if (!m_model) return;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString title = m_model->index(i, 0).data(Qt::DisplayRole).toString();
        if (!title.isEmpty() && title[0].toUpper() == letter.toUpper()) { scrollTo(i); return; }
    }
}

void CinematicCarousel::ApplyTheme() { update(); }

void CinematicCarousel::setControllerFocus(bool focus) { m_has_focus = focus; update(); }

void CinematicCarousel::onNavigated(int dx, int dy) { if (!m_has_focus || !m_model || m_model->rowCount() == 0) return; startSnapAnimation(std::round(m_focal_index + dx)); }

void CinematicCarousel::onActivated() { if (!m_has_focus) return; QModelIndex idx = currentIndex(); if (idx.isValid()) emit itemActivated(idx); }

void CinematicCarousel::onCancelled() {}

void CinematicCarousel::paintEvent(QPaintEvent* event) {
    if (!m_model || m_model->rowCount() == 0) return;
    QPainter p(this); p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    const int count = m_model->rowCount(); const qreal vcx = width() / 2.0; const qreal vcy = height() / 2.0;
    const int is = UISettings::values.game_icon_size.GetValue();
    const float raw_scale = static_cast<float>(is) / 128.0f;
    const float scale = std::max(0.1f, raw_scale);
    const qreal bs = is + (35.0f * scale);
    const qreal arrow_ay = std::max(35.0, vcy - (1.4 * ((is + static_cast<int>(25 * scale)) / 2.0)) - (28.0f * scale));

    QVector<int> order;
    for (int i = 0; i < count; ++i) order << i;
    std::sort(order.begin(), order.end(), [this](int a, int b) { return std::abs(a - m_focal_index) > std::abs(b - m_focal_index); });
    for (int i : order) {
        const qreal d = i - m_focal_index; const qreal dist = std::abs(d * bs);
        if (dist > width()) continue;
        qreal s = 1.0; qreal dx = 0.0;
        const qreal x = vcx + (d * bs) + dx; const qreal y = vcy;
        p.save(); p.translate(x, y);

        // Apply Entry Animation (discovery "pop" effect)
        qreal entry_anim = 1.0;
        QPersistentModelIndex pidx(m_model->index(i, 0));
        if (m_entry_animations.contains(pidx)) {
            entry_anim = m_entry_animations[pidx];
        }
        if (entry_anim < 1.0) {
            qreal pop_s = 0.7 + (entry_anim * 0.3);
            p.scale(pop_s, pop_s);
            p.setOpacity(entry_anim);
        }

        p.scale(s, s);
        const int cs_w = is + static_cast<int>(25 * scale); 
        const int cs_h = is + static_cast<int>(25 * scale);
        QRectF cr(-cs_w / 2.0, -cs_h / 2.0, cs_w, cs_h);
        QPainterPath path; path.addRoundedRect(cr, 16 * scale, 16 * scale);
        const bool focal = std::abs(i - m_focal_index) < 0.5;
        if (focal) {
            p.save(); QColor acc = AccentColor(); qreal pulse = (std::sin(m_pulse_tick * 0.1) + 1.0) / 2.0;
            if (m_has_focus) { p.setPen(QPen(acc, (4.5f + pulse * 1.5f) * scale)); acc.setAlphaF(static_cast<float>(0.12 + pulse * 0.08)); p.setBrush(acc); }
            else { acc.setAlphaF(0.4f); p.setPen(QPen(acc, 3.0f * scale)); p.setBrush(Qt::NoBrush); }
            p.drawPath(path); p.restore();

            // 1. Determine Section Boundary & Header Drawing
            bool draw_header = (i == 0);
            int type = m_model->index(i, 0).data(GameListItem::TypeRole).toInt();
            int prev_type = (i > 0) ? m_model->index(i - 1, 0).data(GameListItem::TypeRole).toInt() : -1;
            bool is_fav = (type == static_cast<int>(GameListItemType::Favorites));
            bool was_fav = (prev_type == static_cast<int>(GameListItemType::Favorites));

            if (i > 0) {
                if (is_fav != was_fav) {
                    draw_header = true;
                } else if (!is_fav) {
                    QString t1 = m_model->index(i, 0).data(Qt::DisplayRole).toString();
                    QString t2 = m_model->index(i - 1, 0).data(Qt::DisplayRole).toString();
                    if (t1.isEmpty() || t2.isEmpty() || t1[0].toUpper() != t2[0].toUpper()) draw_header = true;
                }
            }

            if (draw_header) {
                p.save(); p.resetTransform(); p.setPen(acc); p.setOpacity(Theme::IsDarkMode() ? 0.9 : 1.0);
                qreal header_ay = arrow_ay - (15.0f * scale);
                
                if (is_fav) {
                    QFont hf = font(); hf.setBold(true); hf.setPointSizeF(std::max(1.0f, 24.0f * scale)); p.setFont(hf);
                    QRectF text_rect(x - 300 * scale, header_ay - 150 * scale, 600 * scale, 150 * scale);
                    p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextDontClip, tr("★ FAVORITES"));
                } else {
                    QString title = m_model->index(i, 0).data(Qt::DisplayRole).toString();
                    QChar cl = title.isEmpty() ? QLatin1Char('#') : title[0].toUpper();
                    if (!cl.isLetter()) cl = QLatin1Char('#');
                    QFont hf = font(); hf.setBold(true); hf.setPointSizeF(std::max(1.0f, 48.0f * scale)); p.setFont(hf);
                    QRectF text_rect(x - 200 * scale, header_ay - 200 * scale, 400 * scale, 200 * scale);
                    p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextDontClip, cl);
                }
                p.restore();
            }

            if (i > 0 && was_fav && !is_fav) {
                p.save(); p.setOpacity(0.4); p.setPen(QPen(acc, 2.5f * scale));
                qreal dx_line = -bs / 2.0;
                p.drawLine(QPointF(dx_line, -cs_h / 1.5), QPointF(dx_line, cs_h / 1.5));
                p.restore();
            }
        }
        if (!focal) { p.setPen(Qt::NoPen); p.setBrush(CardBg()); p.setOpacity(0.85); p.drawPath(path); }
        QModelIndex idx = m_model->index(i, 0);
        QPixmap pix = idx.data(GameListItemPath::HighResIconRole).value<QPixmap>();
        if (pix.isNull()) pix = idx.data(Qt::DecorationRole).value<QPixmap>();
        if (!pix.isNull()) {
            p.setOpacity(focal ? 1.0 : 0.85);
            const int pad = 10; QRectF ir(-is / 2.0 + pad / 2.0, -is / 2.0 + pad / 2.0, is - pad, is - pad);
            QPainterPath ip; ip.addRoundedRect(ir, 12, 12);
            p.save(); p.setClipPath(ip); p.drawPixmap(ir, pix, pix.rect()); p.restore();
            p.setPen(QPen(Theme::IsDarkMode() ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 15), 1.0)); p.drawPath(ip);
        }
        p.restore();
    }

    p.save(); QColor acc = AccentColor(); p.setPen(acc); p.setOpacity(0.9);
    qreal arr_size = 12.0f * scale;
    QPainterPath static_arr; static_arr.moveTo(vcx, arrow_ay); 
    static_arr.lineTo(vcx - arr_size, arrow_ay - arr_size); 
    static_arr.lineTo(vcx + arr_size, arrow_ay - arr_size); 
    static_arr.closeSubpath();
    p.drawPath(static_arr); p.restore();
    const int aw = 60, ah = 60;
    auto drawArrow = [&](bool left, bool hover) {
        int ax = left ? 40 : static_cast<int>(width()) - 40 - aw;
        int arrow_y = static_cast<int>(height() - ah) / 2;
        p.save(); p.setOpacity(hover ? 1.0 : 0.4);
        p.setPen(Qt::NoPen); p.setBrush(Theme::IsDarkMode() ? QColor(0,0,0,115) : QColor(0,0,0,15));
        p.drawEllipse(ax, arrow_y, aw, ah);
        p.setPen(QPen(Theme::IsDarkMode() ? Qt::white : QColor(30, 30, 30), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
        QPainterPath ap;
        if (left) { ap.moveTo(ax + aw * 0.65, arrow_y + ah * 0.25); ap.lineTo(ax + aw * 0.35, arrow_y + ah * 0.5); ap.lineTo(ax + aw * 0.65, arrow_y + ah * 0.75); }
        else { ap.moveTo(ax + aw * 0.35, arrow_y + ah * 0.25); ap.lineTo(ax + aw * 0.65, arrow_y + ah * 0.5); ap.lineTo(ax + aw * 0.35, arrow_y + ah * 0.75); }
        p.drawPath(ap); p.restore();
    };
    drawArrow(true, m_left_arrow_hover); drawArrow(false, m_right_arrow_hover);
}

void CinematicCarousel::mousePressEvent(QMouseEvent* event) {
    if (m_left_arrow_hover || m_right_arrow_hover) { m_scroll_timer->start(); return; }
    if (m_snap_animation->state() == QAbstractAnimation::Running) m_snap_animation->stop();
    if (m_momentum_timer->isActive()) m_momentum_timer->stop();
    
    if (event->button() == Qt::LeftButton) {
        m_last_mouse_pos = event->pos(); m_drag_start_pos = event->pos(); m_is_dragging = true;
        m_velocity = 0.0;
        m_last_move_timestamp = QDateTime::currentMSecsSinceEpoch();
    }
}

void CinematicCarousel::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pt = event->pos();
    if (!rect().contains(pt)) {
        if (m_left_arrow_hover || m_right_arrow_hover || m_hover_icon_index != -1) {
            m_left_arrow_hover = false; m_right_arrow_hover = false; m_hover_icon_index = -1; update();
        }
        return;
    }
    bool left = pt.x() < 120 && std::abs(pt.y() - height()/2) < 180;
    bool right = pt.x() > width() - 120 && std::abs(pt.y() - height()/2) < 180;
    if (left != m_left_arrow_hover || right != m_right_arrow_hover) {
        m_left_arrow_hover = left; m_right_arrow_hover = right; update();
    }
    if (!m_is_dragging) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dt = now - m_last_move_timestamp;
    
    qreal dx = m_last_mouse_pos.x() - pt.x();
    qreal delta_index = (dx / (UISettings::values.game_icon_size.GetValue() + 35.0));
    
    if (dt > 0) {
        // Instantaneous velocity (index change per frame)
        m_velocity = (delta_index / static_cast<qreal>(dt)) * 16.0; 
    }

    setFocalIndex(m_focal_index + delta_index);
    m_last_mouse_pos = pt;
    m_last_move_timestamp = now;
}

void CinematicCarousel::mouseReleaseEvent(QMouseEvent* event) {
    m_scroll_timer->stop(); m_is_dragging = false;

    // Handle standard click
    if ((event->pos() - m_drag_start_pos).manhattanLength() < 15) { 
        QModelIndex idx = indexAt(event->pos()); 
        if (idx.isValid()) { startSnapAnimation(idx.row()); return; } 
    }
    
    // Begin momentum glide if velocity is significant
    if (std::abs(m_velocity) > 0.05) {
        m_momentum_timer->start();
    } else {
        startSnapAnimation(std::round(m_focal_index));
    }
}

void CinematicCarousel::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) { QModelIndex idx = iconAt(event->pos()); if (idx.isValid()) emit itemActivated(idx); }
}

QModelIndex CinematicCarousel::iconAt(const QPoint& pt) const {
    const qreal vcx = width() / 2.0; const qreal vcy = height() / 2.0; const int is = UISettings::values.game_icon_size.GetValue();
    const qreal bs = is + 35.0; const qreal th = is * 2.0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const qreal d = i - m_focal_index; const qreal dist = std::abs(d * bs);
        qreal s = 1.0; qreal dx = 0.0;
        if (dist < th) { qreal f = 1.0 - (dist / th); s = 1.0 + (f * 0.40); dx = d * (is / 2.0) * f; }
        const qreal x = vcx + (d * bs) + dx; const qreal fis = is * s;
        QRectF ir(x - fis / 2.0, vcy - fis / 2.0, fis, fis);
        if (ir.contains(pt)) return m_model->index(i, 0);
    }
    return QModelIndex();
}

void CinematicCarousel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_X || event->key() == Qt::Key_Y) {
        QModelIndex cur = currentIndex();
        if (cur.isValid()) {
            int type = cur.data(GameListItem::TypeRole).toInt();
            QString title = cur.data(Qt::DisplayRole).toString();
            QChar cc = title.isEmpty() ? QLatin1Char(' ') : title[0].toUpper();
            int tot = m_model->rowCount(); int sr = cur.row();
            for (int i = 1; i <= tot; ++i) {
                int nr = (sr + i) % tot;
                QModelIndex nidx = m_model->index(nr, 0);
                int nt = nidx.data(GameListItem::TypeRole).toInt();
                if (nt != type) { scrollTo(nr); return; } // Section jump

                if (nt != static_cast<int>(GameListItemType::Favorites)) {
                    QString ntit = nidx.data(Qt::DisplayRole).toString();
                    QChar nc = ntit.isEmpty() ? QLatin1Char(' ') : ntit[0].toUpper();
                    if (nc != cc) { scrollTo(nr); return; } // Alpha jump
                }
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void CinematicCarousel::wheelEvent(QWheelEvent* event) { const int d = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y(); setFocalIndex(m_focal_index - (d / 120.0)); startSnapAnimation(std::round(m_focal_index)); }

void CinematicCarousel::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); update(); }

void CinematicCarousel::startSnapAnimation(qreal target) { m_snap_animation->stop(); m_snap_animation->setStartValue(m_focal_index); m_snap_animation->setEndValue(target); m_snap_animation->start(); }

void CinematicCarousel::updateFocalItem() { if (!m_model) return; int idx = std::round(m_focal_index); if (idx >= 0 && idx < m_model->rowCount()) emit focalItemChanged(m_model->index(idx, 0)); }

void CinematicCarousel::focusOutEvent(QFocusEvent* event) { m_is_dragging = false; m_scroll_timer->stop(); update(); QWidget::focusOutEvent(event); }
void CinematicCarousel::leaveEvent(QEvent* event) { m_is_dragging = false; m_left_arrow_hover = false; m_right_arrow_hover = false; m_hover_icon_index = -1; update(); QWidget::leaveEvent(event); }


QColor CinematicCarousel::CardBg() const { 
    return Theme::IsDarkMode() ? QColor(25, 25, 28, 205) : QColor(240, 240, 245, 180); 
}
QColor CinematicCarousel::TextColor() const { 
    return Theme::IsDarkMode() ? QColor(255, 255, 255) : QColor(45, 45, 48); 
}
QColor CinematicCarousel::AccentColor() const { const QString h = QString::fromStdString(UISettings::values.accent_color.GetValue()); return QColor(h).isValid() ? QColor(h) : QColor(0, 150, 255); }

GameCarouselView::GameCarouselView(QWidget* parent) : QWidget(parent) {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(30, 20, 30, 20);
    m_layout->setSpacing(0);
    m_top_hint = new QLabel(this);
    m_top_hint->setText(tr("if using controller* Press X for Next Alphabetical Letter | Press -/R/ZR for Details Tab | Press B for Back to List"));
    m_top_hint->setAlignment(Qt::AlignCenter);
    m_top_hint->setWordWrap(true);
    m_layout->addSpacing(10);
    m_layout->addWidget(m_top_hint);
    m_layout->addSpacing(30);
    m_carousel = new CinematicCarousel(this);
    m_layout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));
    m_layout->addWidget(m_carousel);
    m_layout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));
    m_bottom_hint = new QLabel(this);
    m_bottom_hint->setText(tr("*You can Drag to Scroll, or Click on Game Icons manually, you can also use your mouse wheel!*"));
    m_bottom_hint->setAlignment(Qt::AlignCenter);
    m_bottom_hint->setWordWrap(true);
    m_layout->addWidget(m_bottom_hint);
    connect(m_carousel, &CinematicCarousel::focalItemChanged, this, &GameCarouselView::itemSelectionChanged);
    connect(m_carousel, &CinematicCarousel::itemActivated, this, &GameCarouselView::itemActivated);
    ApplyTheme();
}

void GameCarouselView::ApplyTheme() { 
    m_carousel->ApplyTheme(); 
    bool dark = Theme::IsDarkMode();
    if (m_top_hint) {
        m_top_hint->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-weight: bold; font-family: 'Outfit', 'Inter', sans-serif; font-size: 14px; }"
        ).arg(dark ? QStringLiteral("rgba(255, 255, 255, 140)") : QStringLiteral("rgba(30, 30, 35, 180)")));
    }
    if (m_bottom_hint) {
        m_bottom_hint->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-style: italic; font-size: 13px; }"
        ).arg(dark ? QStringLiteral("rgba(255, 255, 255, 100)") : QStringLiteral("rgba(30, 30, 35, 120)")));
    }
}
void GameCarouselView::setModel(QAbstractItemModel* model) { m_carousel->setModel(model); }
void GameCarouselView::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); m_carousel->setMinimumHeight((UISettings::values.game_icon_size.GetValue() * 2.0) + 450); }
