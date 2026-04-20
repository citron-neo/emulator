#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>


#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>
#include <QList>
#include <QListView>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>
#include "citron/game_grid_delegate.h"
#include "citron/game_list_p.h"
#include "citron/ui/game_grid_view.h"
#include "citron/uisettings.h"


class WoodBackgroundContainer : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* e) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        int w = width();

        painter.fillRect(e->rect(), QColor(16, 8, 3));

        int panel_w = 90;
        for (int x = 0; x < w; x += panel_w) {
            QRect panel_rect(x, e->rect().top(), panel_w, e->rect().height());
            QLinearGradient panel_grad(x, 0, x + panel_w, 0);

            int var = (x / panel_w) % 3;
            QColor base_color = (var == 0)   ? QColor(36, 18, 8)
                                : (var == 1) ? QColor(30, 14, 6)
                                             : QColor(40, 20, 9);

            panel_grad.setColorAt(0.0, base_color);
            panel_grad.setColorAt(0.2, base_color.lighter(105));
            panel_grad.setColorAt(0.8, base_color.darker(110));
            panel_grad.setColorAt(1.0, QColor(10, 4, 1, 150));

            painter.fillRect(panel_rect, panel_grad);

            painter.setPen(QPen(QColor(15, 6, 2, 60), 1));
            for (int line = 1; line < 4; ++line) {
                int lx = x + line * (panel_w / 4) + (var * 2);
                painter.drawLine(lx, e->rect().top(), lx, e->rect().bottom());
            }
        }
    }
};

class ContentHeightListView : public QListView {
public:
    using QListView::QListView;

protected:
    void wheelEvent(QWheelEvent* e) override {
        e->ignore();
    }
    void paintEvent(QPaintEvent* e) override {
        QPainter painter(this->viewport());
        painter.setRenderHint(QPainter::Antialiasing, false);

        int w = viewport()->width();

        if (this->model() && this->model()->rowCount() > 0) {
            int icon_size = UISettings::values.game_icon_size.GetValue();
            float scale = static_cast<float>(icon_size) / 128.0f;
            int card_h = icon_size + static_cast<int>(64 * scale);

            QList<int> row_tops;
            for (int i = 0; i < this->model()->rowCount(); ++i) {
                QRect r = this->visualRect(this->model()->index(i, 0));
                // If any part of the row's indicator item is visible or near visible, collect its
                // row Y
                if (r.isValid() && r.bottom() >= -200 && r.top() <= viewport()->height() + 200) {
                    if (!row_tops.contains(r.y())) {
                        row_tops.append(r.y());
                    }
                }
            }

            const int shelf_hl = qMax(3, static_cast<int>(5 * scale));
            const int shelf_edge = qMax(1, static_cast<int>(2 * scale));

            for (int ry : row_tops) {
                int shelf_y = ry + static_cast<int>(12 * scale) + card_h;
                int item_h = icon_size + static_cast<int>(104 * scale);
                int shelf_h = ry + item_h - shelf_y;

                QRect shelf_rect(0, shelf_y, w, shelf_h);

                // Drop shadow from back edge
                QLinearGradient back_shadow(0, shelf_y, 0, shelf_y - 12 * scale);
                back_shadow.setColorAt(0.0, QColor(0, 0, 0, 100));
                back_shadow.setColorAt(1.0, Qt::transparent);
                painter.fillRect(QRect(0, shelf_y - 12 * scale, w, 12 * scale), back_shadow);

                painter.fillRect(QRect(0, shelf_y, w, qMax(1, static_cast<int>(4 * scale))),
                                 QColor(0, 0, 0, 180));

                QLinearGradient wood_grad(0, shelf_y, 0, shelf_y + shelf_h);
                wood_grad.setColorAt(0.00, QColor(110, 65, 20));
                wood_grad.setColorAt(0.25, QColor(88, 50, 14));
                wood_grad.setColorAt(0.65, QColor(65, 34, 8));
                wood_grad.setColorAt(1.00, QColor(42, 22, 4));
                painter.fillRect(shelf_rect, wood_grad);

                painter.save();
                painter.setClipRect(shelf_rect);
                for (int g = 0; g < 6; ++g) {
                    int gy = shelf_y + (shelf_h * g) / 6;
                    int alpha = (g % 2 == 0) ? 28 : 14;
                    painter.setPen(QPen(QColor(180, 110, 40, alpha),
                                        qMax(0.8, 0.8 * static_cast<double>(scale))));
                    painter.drawLine(0, gy, w, gy + qMax(1, static_cast<int>(2 * scale)));
                }
                painter.restore();

                QLinearGradient hl_grad(0, shelf_y, 0, shelf_y + shelf_hl);
                hl_grad.setColorAt(0.0, QColor(215, 145, 58, 245));
                hl_grad.setColorAt(0.6, QColor(175, 105, 35, 120));
                hl_grad.setColorAt(1.0, QColor(140, 80, 22, 0));
                painter.setPen(Qt::NoPen);
                painter.setBrush(hl_grad);
                painter.drawRect(QRect(0, shelf_y, w, shelf_hl));

                painter.fillRect(QRect(0, shelf_rect.bottom() - shelf_edge + 1, w, shelf_edge),
                                 QColor(10, 4, 1, 220));
            }
        }
        QListView::paintEvent(e);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        m_press_pos = e->pos();
        m_is_drag_candidate = true;
        // Don't call base class immediately to prevent eager selection on press
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        // If we haven't moved much and the scroller isn't actively moving, 
        // then this is a valid click.
        if (m_is_drag_candidate && (e->pos() - m_press_pos).manhattanLength() < 10) {
            QListView::mousePressEvent(e); // Trigger selection
            QListView::mouseReleaseEvent(e); // Trigger activation
        }
        m_is_drag_candidate = false;
    }

private:
    QPoint m_press_pos;
    bool m_is_drag_candidate = false;
};

GameGridView::GameGridView(QWidget* parent) : QWidget(parent) {
    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    m_container = new WoodBackgroundContainer(m_scroll_area);
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(0, 20, 0, 20);
    m_layout->setSpacing(0);

    m_top_help = new QLabel(m_container);
    m_top_help->setText(tr("if using controller* Press X for Next Alphabetical Letter | Press "
                           "-/R/ZR for Details Tab | Press B/L/ZL for Go Back"));
    m_top_help->setStyleSheet(
        QStringLiteral("QLabel { color: rgba(255, 255, 255, 140); font-weight: bold; font-family: "
                       "'Outfit', 'Inter', sans-serif; font-size: 14px; }"));
    m_top_help->setAlignment(Qt::AlignCenter);
    m_layout->addSpacing(10);
    m_layout->addWidget(m_top_help);
    m_layout->addSpacing(20);

    auto setupView = [&](QListView*& view, GameGridDelegate*& delegate) {
        view = new ContentHeightListView(m_container);
        view->setViewMode(QListView::IconMode);
        view->setResizeMode(QListView::Adjust);
        view->setFlow(QListView::LeftToRight);
        view->setWrapping(true);
        view->setFrameStyle(QFrame::NoFrame);
        view->setAttribute(Qt::WA_MacShowFocusRect, false);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setDragEnabled(false);
        view->setAcceptDrops(false);
        view->setFocusPolicy(Qt::StrongFocus);
        delegate = new GameGridDelegate(view, this);
        view->setItemDelegate(delegate);
        connect(view, &QListView::activated, this, &GameGridView::itemActivated);
        connect(view, &QListView::doubleClicked, this, &GameGridView::itemActivated);
    };

    m_fav_label = new QLabel(tr("★  FAVORITES"), m_container);
    m_fav_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #f5f5f5; font-weight: bold; font-size: 16px; "
        "padding: 8px 0px 6px 28px; "
        "border-bottom: 2px solid rgba(255, 255, 255, 90); background: transparent; }"));
    QGraphicsDropShadowEffect* fav_shadow = new QGraphicsDropShadowEffect(m_fav_label);
    fav_shadow->setBlurRadius(3);
    fav_shadow->setOffset(1, 1);
    fav_shadow->setColor(QColor(0, 0, 0, 255));
    m_fav_label->setGraphicsEffect(fav_shadow);
    m_layout->addWidget(m_fav_label);
    setupView(m_fav_view, m_fav_delegate);
    m_fav_view->setStyleSheet(
        QStringLiteral("QListView { background: transparent; border: none; }"
                       "QListView::item { background: transparent; }"
                       "QListView::item:hover { background: transparent; }"
                       "QListView::item:selected { background: transparent; }"));
    m_layout->addWidget(m_fav_view);

    m_main_label = new QLabel(tr("ALL GAMES"), m_container);
    m_main_label->setStyleSheet(
        QStringLiteral("QLabel { color: #f5f5f5; font-weight: bold; font-size: 16px; "
                       "padding: 8px 0px 6px 28px; "
                       "border-bottom: 2px solid rgba(255, 255, 255, 90); background: transparent; "
                       "margin-top: 18px; }"));
    QGraphicsDropShadowEffect* main_shadow = new QGraphicsDropShadowEffect(m_main_label);
    main_shadow->setBlurRadius(3);
    main_shadow->setOffset(1, 1);
    main_shadow->setColor(QColor(0, 0, 0, 255));
    m_main_label->setGraphicsEffect(main_shadow);
    m_layout->addWidget(m_main_label);
    setupView(m_main_view, m_main_delegate);
    m_main_view->setStyleSheet(
        QStringLiteral("QListView { background: transparent; border: none; }"
                       "QListView::item { background: transparent; }"
                       "QListView::item:hover { background: transparent; }"
                       "QListView::item:selected { background: transparent; }"));
    m_layout->addWidget(m_main_view);

    m_bottom_hint = new QLabel(m_container);
    m_bottom_hint->setText(
        tr("*You can use your Mouse Wheel or the Scrollbar to navigate the Grid View*"));
    m_bottom_hint->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255, 255, 255, 100); font-style: italic; font-size: 13px; }"));
    m_bottom_hint->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_bottom_hint);
    m_scroll_area->setWidget(m_container);
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(m_scroll_area);

    ApplyTheme();
}

void GameGridView::ApplyTheme() {
    m_container->setAutoFillBackground(false);
    m_scroll_area->setAutoFillBackground(false);
    if (m_scroll_area->viewport()) {
        m_scroll_area->viewport()->setAutoFillBackground(false);
        QPalette vpal = m_scroll_area->viewport()->palette();
        vpal.setColor(QPalette::Base, Qt::transparent);
        vpal.setColor(QPalette::Window, Qt::transparent);
        m_scroll_area->viewport()->setPalette(vpal);
    }
    m_scroll_area->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; } QScrollArea > "
                       "QWidget > QWidget { background: transparent; }"));

    QString list_style = QStringLiteral(
        "QListView { background: transparent; border: none; outline: 0; padding: 0px; }"
        "QListView::item { padding: 0px; margin: 0px; background: transparent; }"
        "QListView::item:hover { background: transparent; }"
        "QListView::item:selected { background: transparent; }");
    m_fav_view->setStyleSheet(list_style);
    m_main_view->setStyleSheet(list_style);
}

void GameGridView::setModels(QAbstractItemModel* fav_model, QAbstractItemModel* main_model) {
    m_fav_view->setModel(fav_model);
    m_main_view->setModel(main_model);

    bool has_favs = fav_model && fav_model->rowCount() > 0;
    m_fav_label->setVisible(has_favs);
    m_fav_view->setVisible(has_favs);

    if (m_fav_view->selectionModel()) {
        connect(m_fav_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex& current) {
                    if (current.isValid()) {
                        emit itemSelectionChanged(current);
                        m_main_view->clearSelection();
                        m_main_view->setCurrentIndex(QModelIndex());
                    }
                });
    }
    if (m_main_view->selectionModel()) {
        connect(m_main_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex& current) {
                    if (current.isValid()) {
                        emit itemSelectionChanged(current);
                        m_fav_view->clearSelection();
                        m_fav_view->setCurrentIndex(QModelIndex());
                    }
                });
    }
    UpdateGridSize();
}

QModelIndex GameGridView::currentIndex() const {
    if (m_fav_view->hasFocus())
        return m_fav_view->currentIndex();
    return m_main_view->currentIndex();
}

void GameGridView::setCurrentIndex(const QModelIndex& index) {
    if (index.model() == m_fav_view->model())
        m_fav_view->setCurrentIndex(index);
    else
        m_main_view->setCurrentIndex(index);
}

QModelIndex GameGridView::indexAt(const QPoint& p) const {
    QPoint fp = m_fav_view->mapFrom(this, p);
    if (m_fav_view->rect().contains(fp))
        return m_fav_view->indexAt(fp);
    QPoint mp = m_main_view->mapFrom(this, p);
    if (m_main_view->rect().contains(mp))
        return m_main_view->indexAt(mp);
    return QModelIndex();
}

void GameGridView::setControllerFocus(bool focus) {
    m_has_focus = focus;
    if (focus) {
        // Preserve existing selection if possible
        if (m_fav_view->currentIndex().isValid()) {
            m_fav_view->setFocus();
            return;
        }
        if (m_main_view->currentIndex().isValid()) {
            m_main_view->setFocus();
            return;
        }

        // Default to first available item
        if (m_fav_view->isVisible() && m_fav_view->model()->rowCount() > 0) {
            m_fav_view->setFocus();
            m_fav_view->setCurrentIndex(m_fav_view->model()->index(0, 0));
        } else if (m_main_view->model() && m_main_view->model()->rowCount() > 0) {
            m_main_view->setFocus();
            m_main_view->setCurrentIndex(m_main_view->model()->index(0, 0));
        }
    }
}


void GameGridView::onNavigated(int dx, int dy) {
    if (!m_has_focus)
        return;
    QListView* current = m_fav_view->hasFocus() ? m_fav_view : m_main_view;
    if (!current->model())
        return;

    QModelIndex idx = current->currentIndex();
    if (!idx.isValid()) {
        current->setCurrentIndex(current->model()->index(0, 0));
        return;
    }

    int row = idx.row();
    int total = current->model()->rowCount();
    int cols = qMax(1, current->viewport()->width() / current->gridSize().width());

    if (dy > 0 && row + cols >= total && current == m_fav_view) {
        m_main_view->setFocus();
        m_main_view->setCurrentIndex(m_main_view->model()->index(0, 0));
        return;
    }
    if (dy < 0 && row < cols && current == m_main_view && m_fav_view->isVisible()) {
        m_fav_view->setFocus();
        m_fav_view->setCurrentIndex(
            m_fav_view->model()->index(m_fav_view->model()->rowCount() - 1, 0));
        return;
    }

    if (dx > 0)
        row++;
    else if (dx < 0)
        row--;
    if (dy > 0)
        row += cols;
    else if (dy < 0)
        row -= cols;

    row = qBound(0, row, total - 1);
    current->setCurrentIndex(current->model()->index(row, 0));
    scrollTo(current->currentIndex());
}

void GameGridView::onActivated() {
    if (!m_has_focus)
        return;
    QModelIndex cur = currentIndex();
    if (cur.isValid())
        emit itemActivated(cur);
}

void GameGridView::onCancelled() {
    m_has_focus = false;
    m_fav_view->clearFocus();
    m_main_view->clearFocus();
}

void GameGridView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_X || event->key() == Qt::Key_Y) {
        QListView* current = m_fav_view->hasFocus() ? m_fav_view : m_main_view;
        if (current == m_fav_view && m_main_view->model()->rowCount() > 0) {
            m_main_view->setFocus();
            m_main_view->setCurrentIndex(m_main_view->model()->index(0, 0));
            return;
        }
        if (!current->model())
            return;
        QModelIndex cur = current->currentIndex();
        if (!cur.isValid())
            return;
        QString title = cur.data(Qt::DisplayRole).toString();
        QChar cc = title.isEmpty() ? QLatin1Char(' ') : title[0].toUpper();
        int tot = current->model()->rowCount();
        int sr = cur.row();
        for (int i = 1; i <= tot; ++i) {
            int nr = (sr + i) % tot;
            QModelIndex nidx = current->model()->index(nr, 0);
            QString nt = nidx.data(Qt::DisplayRole).toString();
            QChar nc = nt.isEmpty() ? QLatin1Char(' ') : nt[0].toUpper();
            if (nc != cc) {
                current->setCurrentIndex(nidx);
                current->scrollTo(nidx);
                return;
            }
        }
    }
}

void GameGridView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(10, this, &GameGridView::UpdateGridSize);
}

void GameGridView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateGridSize();
}

void GameGridView::UpdateGridSize() {
    if (!m_scroll_area || !m_scroll_area->viewport())
        return;
    const int is = UISettings::values.game_icon_size.GetValue();
    const float s = static_cast<float>(is) / 128.0f;
    const int bw = qMax(is + static_cast<int>(40 * s), is + 24);
    const int tw = m_scroll_area->viewport()->width();

    // If width is too small (widget hidden or layouting), retry shortly
    if (tw < 100) {
        QTimer::singleShot(100, this, &GameGridView::UpdateGridSize);
        return;
    }

    if (tw > 50) {
        int cols = qMax(1, tw / bw);
        int aw = tw / cols;
        const int item_h = qMax(is + static_cast<int>(85 * s), is + 40);
        QSize gs(aw, item_h);
        m_fav_view->setGridSize(gs);
        m_main_view->setGridSize(gs);
        m_fav_view->setFixedWidth(tw);
        m_main_view->setFixedWidth(tw);
    }
    UpdateLayoutHeights();
}

void GameGridView::UpdateLayoutHeights() {
    auto updateHeight = [&](QListView* view) {
        if (!view || !view->isVisible() || !view->model() || view->model()->rowCount() == 0) {
            view->setFixedHeight(0);
            return;
        }
        const int count = view->model()->rowCount();
        const QSize gs = view->gridSize();
        if (gs.width() <= 0 || gs.height() <= 0)
            return;
        const int tw = m_scroll_area->viewport()->width();
        const int cols = qMax(1, tw / gs.width());
        const int rows = (count + cols - 1) / cols;
        view->setFixedHeight(rows * gs.height() + 60);
    };
    updateHeight(m_fav_view);
    updateHeight(m_main_view);
}

QAbstractItemModel* GameGridView::favModel() const {
    return m_fav_view->model();
}
QAbstractItemModel* GameGridView::mainModel() const {
    return m_main_view->model();
}

QItemSelectionModel* GameGridView::selectionModel() const {
    return m_main_view->selectionModel();
}

void GameGridView::setModel(QAbstractItemModel* model) {
    m_main_view->setModel(model);
    m_fav_view->setModel(nullptr);
    m_fav_label->hide();
    m_fav_view->hide();
    UpdateGridSize();
}
QRect GameGridView::visualRect(const QModelIndex& index) const {
    if (index.model() == m_fav_view->model())
        return m_fav_view->visualRect(index);
    return m_main_view->visualRect(index);
}
QWidget* GameGridView::viewport() const {
    return m_scroll_area->viewport();
}
void GameGridView::scrollTo(const QModelIndex& index) {
    QListView* view = (index.model() == m_fav_view->model()) ? m_fav_view : m_main_view;
    QRect rect = view->visualRect(index);
    rect.moveTop(rect.top() + view->y());
    m_scroll_area->ensureVisible(rect.center().x(), rect.center().y(), 50, 50);
}
