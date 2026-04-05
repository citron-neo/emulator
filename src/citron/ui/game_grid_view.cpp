#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QAbstractItemModel>
#include <QLabel>
#include <QItemSelectionModel>

#include <QScrollArea>
#include "citron/ui/game_grid_view.h"
#include "citron/game_grid_delegate.h"
#include "citron/game_list_p.h"
#include "citron/uisettings.h"

class ContentHeightListView : public QListView {
public:
    using QListView::QListView;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

GameGridView::GameGridView(QWidget* parent) : QWidget(parent) {
    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    m_container = new QWidget(m_scroll_area);
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(0, 20, 0, 20);
    m_layout->setSpacing(0);

    m_top_help = new QLabel(m_container);
    m_top_help->setText(tr("if using controller* Press X for Next Alphabetical Letter | Press -/R/ZR for Details Tab | Press B for Back to List"));
    m_top_help->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 140); font-weight: bold; font-family: 'Outfit', 'Inter', sans-serif; font-size: 14px; }"));
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
    };

    m_fav_label = new QLabel(tr("★ FAVORITES"), m_container);
    m_fav_label->setStyleSheet(QStringLiteral("QLabel { color: #ffd700; font-weight: bold; font-size: 18px; padding-left: 24px; margin-bottom: 10px; }"));
    m_layout->addWidget(m_fav_label);
    setupView(m_fav_view, m_fav_delegate);
    m_layout->addWidget(m_fav_view);

    m_main_label = new QLabel(tr("ALL GAMES"), m_container);
    m_main_label->setStyleSheet(QStringLiteral("QLabel { color: #0096ff; font-weight: bold; font-size: 18px; padding-left: 24px; margin-top: 20px; margin-bottom: 10px; }"));
    m_layout->addWidget(m_main_label);
    setupView(m_main_view, m_main_delegate);
    m_layout->addWidget(m_main_view);

    m_bottom_hint = new QLabel(m_container);
    m_bottom_hint->setText(tr("*You can use your Mouse Wheel or the Scrollbar to navigate the Grid View*"));
    m_bottom_hint->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 100); font-style: italic; font-size: 13px; }"));
    m_bottom_hint->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_bottom_hint);

    m_scroll_area->setWidget(m_container);
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(m_scroll_area);

    ApplyTheme();
}

void GameGridView::ApplyTheme() {
    QString style = QStringLiteral(
        "QListView { background: transparent; border: none; outline: 0; padding: 0px; }"
        "QListView::item { padding: 0px; margin: 0px; background: transparent; }"
    );
    m_fav_view->setStyleSheet(style);
    m_main_view->setStyleSheet(style);
}

void GameGridView::setModels(QAbstractItemModel* fav_model, QAbstractItemModel* main_model) {
    m_fav_view->setModel(fav_model);
    m_main_view->setModel(main_model);

    bool has_favs = fav_model && fav_model->rowCount() > 0;
    m_fav_label->setVisible(has_favs);
    m_fav_view->setVisible(has_favs);

    if (m_fav_view->selectionModel()) {
        connect(m_fav_view->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex& current) {
                    if (current.isValid()) {
                        emit itemSelectionChanged(current);
                        m_main_view->clearSelection();
                        m_main_view->setCurrentIndex(QModelIndex());
                    }
                });
    }
    if (m_main_view->selectionModel()) {
        connect(m_main_view->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex& current) {
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
    if (m_fav_view->hasFocus()) return m_fav_view->currentIndex();
    return m_main_view->currentIndex();
}

void GameGridView::setCurrentIndex(const QModelIndex& index) {
    if (index.model() == m_fav_view->model()) m_fav_view->setCurrentIndex(index);
    else m_main_view->setCurrentIndex(index);
}

QModelIndex GameGridView::indexAt(const QPoint& p) const {
    QPoint fp = m_fav_view->mapFrom(this, p);
    if (m_fav_view->rect().contains(fp)) return m_fav_view->indexAt(fp);
    QPoint mp = m_main_view->mapFrom(this, p);
    if (m_main_view->rect().contains(mp)) return m_main_view->indexAt(mp);
    return QModelIndex();
}

void GameGridView::setControllerFocus(bool focus) {
    m_has_focus = focus;
    if (focus) {
        if (m_fav_view->isVisible() && m_fav_view->model()->rowCount() > 0) {
            m_fav_view->setFocus();
            if (!m_fav_view->currentIndex().isValid()) m_fav_view->setCurrentIndex(m_fav_view->model()->index(0, 0));
        } else {
            m_main_view->setFocus();
            if (!m_main_view->currentIndex().isValid()) m_main_view->setCurrentIndex(m_main_view->model()->index(0, 0));
        }
    }
}

void GameGridView::onNavigated(int dx, int dy) {
    if (!m_has_focus) return;
    QListView* current = m_fav_view->hasFocus() ? m_fav_view : m_main_view;
    if (!current->model()) return;

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
        m_fav_view->setCurrentIndex(m_fav_view->model()->index(m_fav_view->model()->rowCount() - 1, 0));
        return;
    }

    if (dx > 0) row++;
    else if (dx < 0) row--;
    if (dy > 0) row += cols;
    else if (dy < 0) row -= cols;

    row = qBound(0, row, total - 1);
    current->setCurrentIndex(current->model()->index(row, 0));
    scrollTo(current->currentIndex());
}

void GameGridView::onActivated() {
    if (!m_has_focus) return;
    QModelIndex cur = currentIndex();
    if (cur.isValid()) emit itemActivated(cur);
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
        if (!current->model()) return;
        QModelIndex cur = current->currentIndex();
        if (!cur.isValid()) return;
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

void GameGridView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateGridSize();
}

void GameGridView::UpdateGridSize() {
    const int is = UISettings::values.game_icon_size.GetValue();
    const float s = static_cast<float>(is) / 128.0f;
    const int bw = qMax(is + static_cast<int>(40 * s), is + 24);
    const int tw = m_scroll_area->viewport()->width();
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
        if (gs.width() <= 0 || gs.height() <= 0) return;
        const int tw = m_scroll_area->viewport()->width();
        const int cols = qMax(1, tw / gs.width());
        const int rows = (count + cols - 1) / cols;
        view->setFixedHeight(rows * gs.height() + 40);
    };
    updateHeight(m_fav_view);
    updateHeight(m_main_view);
}

QAbstractItemModel* GameGridView::favModel() const { return m_fav_view->model(); }
QAbstractItemModel* GameGridView::mainModel() const { return m_main_view->model(); }

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
    if (index.model() == m_fav_view->model()) return m_fav_view->visualRect(index);
    return m_main_view->visualRect(index);
}
QWidget* GameGridView::viewport() const { return m_scroll_area->viewport(); }
void GameGridView::scrollTo(const QModelIndex& index) {
    QListView* view = (index.model() == m_fav_view->model()) ? m_fav_view : m_main_view;
    QRect rect = view->visualRect(index);
    rect.moveTop(rect.top() + view->y());
    m_scroll_area->ensureVisible(rect.center().x(), rect.center().y(), 50, 50);
}

