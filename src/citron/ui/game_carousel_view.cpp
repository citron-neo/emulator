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
#include "citron/uisettings.h"
#include "citron/game_list_p.h"

CinematicCarousel::CinematicCarousel(QWidget* parent) : QWidget(parent) {
    m_snap_animation = new QPropertyAnimation(this, "focalIndex");
    m_snap_animation->setDuration(350);
    m_snap_animation->setEasingCurve(QEasingCurve::OutCubic);
    m_pulse_timer = new QTimer(this);
    connect(m_pulse_timer, &QTimer::timeout, this, [this]{ m_pulse_tick++; update(); });
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

void CinematicCarousel::setModel(QAbstractItemModel* model) { m_model = model; if (m_model && m_model->rowCount() > 0) setFocalIndex(0.0); update(); }

void CinematicCarousel::setFocalIndex(qreal index) {
    if (!m_model || m_model->rowCount() == 0) m_focal_index = 0.0;
    else m_focal_index = std::max(0.0, std::min(static_cast<qreal>(m_model->rowCount() - 1), index));
    updateFocalItem(); update();
}

void CinematicCarousel::scrollTo(int index) { if (!m_model || index < 0 || index >= m_model->rowCount()) return; startSnapAnimation(index); }

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
    const int is = UISettings::values.game_icon_size.GetValue(); const qreal bs = is + 35.0; const qreal th = is * 2.0;
    QVector<int> order;
    for (int i = 0; i < count; ++i) order << i;
    std::sort(order.begin(), order.end(), [this](int a, int b) { return std::abs(a - m_focal_index) > std::abs(b - m_focal_index); });
    for (int i : order) {
        const qreal d = i - m_focal_index; const qreal dist = std::abs(d * bs);
        if (dist > width()) continue;
        qreal s = 1.0; qreal dx = 0.0;
        if (dist < th) { qreal f = 1.0 - (dist / th); s = 1.0 + (f * 0.40); dx = d * (is / 2.0) * f; }
        const qreal x = vcx + (d * bs) + dx; const qreal y = vcy;
        p.save(); p.translate(x, y); p.scale(s, s);
        const int cs_w = is + 25; const int cs_h = is + 25;
        QRectF cr(-cs_w / 2.0, -cs_h / 2.0, cs_w, cs_h);
        QPainterPath path; path.addRoundedRect(cr, 16, 16);
        const bool focal = std::abs(i - m_focal_index) < 0.5;
        if (focal) {
            p.save(); QColor acc = AccentColor(); qreal pulse = (std::sin(m_pulse_tick * 0.1) + 1.0) / 2.0;
            if (m_has_focus) { p.setPen(QPen(acc, 4.5 + pulse * 1.5)); acc.setAlphaF(0.12 + pulse * 0.08); p.setBrush(acc); }
            else { acc.setAlphaF(0.4); p.setPen(QPen(acc, 3.0)); p.setBrush(Qt::NoBrush); }
            p.drawPath(path); p.restore();

            // Draw the alphabetical header ONLY when it changes (category boundary)
            bool draw_header = (i == 0);
            if (i > 0) {
                QString t1 = m_model->index(i, 0).data(Qt::DisplayRole).toString();
                QString t2 = m_model->index(i - 1, 0).data(Qt::DisplayRole).toString();
                if (t1.isEmpty() || t2.isEmpty() || t1[0].toUpper() != t2[0].toUpper()) draw_header = true;
            }
            if (draw_header) {
                p.save(); p.resetTransform(); p.setPen(acc); p.setOpacity(0.9);
                qreal ay = std::max(90.0, y - (s * (cs_h / 2.0)) - 42);
                QString title = m_model->index(i, 0).data(Qt::DisplayRole).toString();
                QChar cl = title.isEmpty() ? QLatin1Char('#') : title[0].toUpper();
                if (!cl.isLetter()) cl = QLatin1Char('#');
                QFont hf = font(); hf.setBold(true); hf.setPointSizeF(48.0); p.setFont(hf); p.setPen(acc);
                p.drawText(QRectF(x - 80, ay - 130, 160, 100), Qt::AlignCenter, cl); p.restore();
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
            p.setPen(QPen(QColor(255, 255, 255, 30), 1.0)); p.drawPath(ip);
        }
        p.restore();
    }

    // Static Down Arrow Indicator (ALWAYS centered at the top)
    p.save(); QColor acc = AccentColor(); p.setPen(acc); p.setOpacity(0.9);
    qreal ay = std::max(55.0, vcy - (1.4 * ((is + 60) / 2.0)) - 28);
    QPainterPath static_arr; static_arr.moveTo(vcx, ay); static_arr.lineTo(vcx - 12, ay - 12); static_arr.lineTo(vcx + 12, ay - 12); static_arr.closeSubpath();
    p.drawPath(static_arr); p.restore();
    const int aw = 60, ah = 60;
    auto drawArrow = [&](bool left, bool hover) {
        int ax = left ? 40 : static_cast<int>(width()) - 40 - aw;
        int arrow_y = static_cast<int>(height() - ah) / 2;
        p.save(); p.setOpacity(hover ? 1.0 : 0.4);
        p.setPen(Qt::NoPen); p.setBrush(QColor(0,0,0,115));
        p.drawEllipse(ax, arrow_y, aw, ah);
        p.setPen(QPen(Qt::white, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
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
    m_last_mouse_pos = event->pos(); m_drag_start_pos = event->pos(); m_is_dragging = true;
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
    qreal dx = m_last_mouse_pos.x() - pt.x();
    setFocalIndex(m_focal_index + (dx / (UISettings::values.game_icon_size.GetValue() + 35.0)));
    m_last_mouse_pos = pt;
}

void CinematicCarousel::mouseReleaseEvent(QMouseEvent* event) {
    m_scroll_timer->stop(); m_is_dragging = false;
    if ((event->pos() - m_drag_start_pos).manhattanLength() < 15) { QModelIndex idx = indexAt(event->pos()); if (idx.isValid()) { startSnapAnimation(idx.row()); return; } }
    startSnapAnimation(std::round(m_focal_index));
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
            QString title = cur.data(Qt::DisplayRole).toString();
            QChar cc = title.isEmpty() ? QLatin1Char(' ') : title[0].toUpper();
            int tot = m_model->rowCount(); int sr = cur.row();
            for (int i = 1; i <= tot; ++i) {
                int nr = (sr + i) % tot;
                QString nt = m_model->index(nr, 0).data(Qt::DisplayRole).toString();
                QChar nc = nt.isEmpty() ? QLatin1Char(' ') : nt[0].toUpper();
                if (nc != cc) { scrollTo(nr); return; }
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void CinematicCarousel::wheelEvent(QWheelEvent* event) { const int d = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y(); setFocalIndex(m_focal_index - (d / 120.0)); startSnapAnimation(std::round(m_focal_index)); }

void CinematicCarousel::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); update(); }

void CinematicCarousel::startSnapAnimation(qreal target) { m_snap_animation->stop(); m_snap_animation->setStartValue(m_focal_index); m_snap_animation->setEndValue(target); m_snap_animation->start(); }

void CinematicCarousel::updateFocalItem() { if (!m_model) return; int idx = std::round(m_focal_index); if (idx >= 0 && idx < m_model->rowCount()) emit focalItemChanged(m_model->index(idx, 0)); }

QColor CinematicCarousel::CardBg() const { return QColor(25, 25, 28, 205); }
QColor CinematicCarousel::TextColor() const { return QColor(255, 255, 255); }
QColor CinematicCarousel::AccentColor() const { const QString h = QString::fromStdString(UISettings::values.accent_color.GetValue()); return QColor(h).isValid() ? QColor(h) : QColor(0, 150, 255); }

GameCarouselView::GameCarouselView(QWidget* parent) : QWidget(parent) {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(30, 20, 30, 20);
    m_layout->setSpacing(0);
    auto* th = new QLabel(this);
    th->setText(tr("if using controller* Press X for Next Alphabetical Letter | Press -/R/ZR for Details Tab | Press B for Back to List"));
    th->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 140); font-weight: bold; font-family: 'Outfit', 'Inter', sans-serif; font-size: 14px; }"));
    th->setAlignment(Qt::AlignCenter);
    m_layout->addSpacing(10);
    m_layout->addWidget(th);
    m_layout->addSpacing(30);
    m_carousel = new CinematicCarousel(this);
    m_layout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));
    m_layout->addWidget(m_carousel);
    m_layout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));
    auto* bh = new QLabel(this);
    bh->setText(tr("*You can Drag to Scroll, or Click on Game Icons manually, you can also use your mouse wheel!*"));
    bh->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 100); font-style: italic; font-size: 13px; }"));
    bh->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(bh);
    connect(m_carousel, &CinematicCarousel::focalItemChanged, this, &GameCarouselView::itemSelectionChanged);
    connect(m_carousel, &CinematicCarousel::itemActivated, this, &GameCarouselView::itemActivated);
    ApplyTheme();
}

void GameCarouselView::ApplyTheme() { m_carousel->ApplyTheme(); }
void GameCarouselView::setModel(QAbstractItemModel* model) { m_carousel->setModel(model); }
void GameCarouselView::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); m_carousel->setMinimumHeight(UISettings::values.game_icon_size.GetValue() + 380); }
