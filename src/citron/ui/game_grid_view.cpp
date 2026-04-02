#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QLabel>
#include <QItemSelectionModel>

#include "citron/ui/game_grid_view.h"
#include "citron/game_grid_delegate.h"
#include "citron/uisettings.h"

GameGridView::GameGridView(QWidget* parent) : QWidget(parent) {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(30, 20, 30, 5); // Tighter bottom margin
    m_layout->setSpacing(0);

    // --- TOP HELP OVERLAY ---
    m_top_help = new QLabel(this);
    m_top_help->setText(tr("if using controller* Press X for Next Alphabetical Letter | Press -/R/ZR for Details Tab | Press B for Back to List"));
    m_top_help->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 140); font-weight: bold; font-family: 'Outfit', 'Inter', sans-serif; font-size: 14px; }"));
    m_top_help->setAlignment(Qt::AlignCenter);
    m_layout->addSpacing(10);
    m_layout->addWidget(m_top_help);
    m_layout->addSpacing(30);

    m_view = new QListView(this);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setFlow(QListView::LeftToRight);
    m_view->setWrapping(true);
    m_view->setSpacing(20);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setAttribute(Qt::WA_MacShowFocusRect, false);
    
    // Modernized ScrollBar Styling
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Disable traditional Drag & Drop to ensure Kinetic Scroll takes precedence
    m_view->setDragEnabled(false);
    m_view->setAcceptDrops(false);
    m_view->setDropIndicatorShown(false);
    m_view->setDefaultDropAction(Qt::IgnoreAction);

    m_delegate = new GameGridDelegate(m_view, this);
    m_view->setItemDelegate(m_delegate);

    m_layout->addWidget(m_view, 1);

    // --- BOTTOM HINT OVERLAY ---
    m_bottom_hint = new QLabel(this);
    m_bottom_hint->setText(tr("*You can use your Mouse Wheel or the Scrollbar to navigate the Grid View*"));
    m_bottom_hint->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 100); font-style: italic; font-size: 13px; }"));
    m_bottom_hint->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_bottom_hint);

    ApplyTheme();
    
    connect(m_view, &QListView::activated, this, &GameGridView::itemActivated);
}

void GameGridView::ApplyTheme() {
    m_view->setStyleSheet(QStringLiteral(
        "QListView {"
        "    background: transparent;"
        "    border: none;"
        "    outline: 0;"
        "    padding: 20px;"
        "}"
        "QListView::item {"
        "    padding: 0px;"
        "    margin: 4px;"
        "    background: transparent;"
        "}"
    ));
}

QAbstractItemModel* GameGridView::model() const {
    return m_view->model();
}

QItemSelectionModel* GameGridView::selectionModel() const {
    return m_view->selectionModel();
}

QRect GameGridView::visualRect(const QModelIndex& index) const {
    return m_view->visualRect(index);
}

QWidget* GameGridView::viewport() const {
    return m_view->viewport();
}

void GameGridView::scrollTo(const QModelIndex& index) {
    m_view->scrollTo(index);
}

void GameGridView::setControllerFocus(bool focus) {
    m_has_focus = focus;
    if (focus) {
        m_view->setFocus();
        if (!m_view->currentIndex().isValid() && m_view->model() && m_view->model()->rowCount() > 0) {
            m_view->setCurrentIndex(m_view->model()->index(0, 0));
        }
    }
}

void GameGridView::onNavigated(int dx, int dy) {
    if (!m_has_focus || !m_view->model()) return;
    
    QModelIndex current = m_view->currentIndex();
    if (!current.isValid()) {
        m_view->setCurrentIndex(m_view->model()->index(0, 0));
        return;
    }

    // Grid navigation logic (simplified for now, can be improved)
    int row = current.row();
    int total = m_view->model()->rowCount();
    
    if (dx > 0) row++;
    else if (dx < 0) row--;
    
    if (dy > 0) {
        // Find how many items per row
        int icons_per_row = qMax(1, m_view->viewport()->width() / m_view->gridSize().width());
        row += icons_per_row;
    } else if (dy < 0) {
        int icons_per_row = qMax(1, m_view->viewport()->width() / m_view->gridSize().width());
        row -= icons_per_row;
    }
    
    row = qBound(0, row, total - 1);
    m_view->setCurrentIndex(m_view->model()->index(row, 0));
    m_view->scrollTo(m_view->currentIndex());
}

void GameGridView::onActivated() {
    if (!m_has_focus) return;
    QModelIndex current = m_view->currentIndex();
    if (current.isValid()) {
        emit itemActivated(current);
    }
}

void GameGridView::onCancelled() {
    if (!m_has_focus) return;
    m_has_focus = false;
    m_view->clearFocus();
}

void GameGridView::setModel(QAbstractItemModel* model) {
    m_view->setModel(model);
    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex& current) {
            emit itemSelectionChanged(current);
        });
    }
}

void GameGridView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_X || event->key() == Qt::Key_Y) {
        if (!m_view->model()) return;
        
        QModelIndex current = m_view->currentIndex();
        if (!current.isValid()) {
            if (m_view->model()->rowCount() > 0) {
                m_view->setCurrentIndex(m_view->model()->index(0, 0));
                return;
            }
        }
        
        QString title = current.data(Qt::DisplayRole).toString();
        QChar current_char = title.isEmpty() ? QLatin1Char(' ') : title[0].toUpper();
        
        int total = m_view->model()->rowCount();
        int start_row = current.row();
        for (int i = 1; i <= total; ++i) {
            int next_row = (start_row + i) % total;
            QString next_title = m_view->model()->index(next_row, 0).data(Qt::DisplayRole).toString();
            QChar next_char = next_title.isEmpty() ? QLatin1Char(' ') : next_title[0].toUpper();
            if (next_char != current_char) {
                m_view->setCurrentIndex(m_view->model()->index(next_row, 0));
                m_view->scrollTo(m_view->model()->index(next_row, 0));
                return;
            }
        }
    }
}

void GameGridView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Adaptive Grid Scaling: Eliminate the Gap
    const int icon_size = UISettings::values.game_icon_size.GetValue();
    const int base_width = icon_size + 40;
    const int total_width = m_view->viewport()->width() - 30; // margins
    
    if (total_width > base_width) {
        int columns = qMax(1, total_width / base_width);
        int adaptive_width = total_width / columns;
        m_view->setGridSize(QSize(adaptive_width, base_width + 45));
    }
}
