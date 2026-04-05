#include "citron/ui/game_tree_view.h"
#include <QMouseEvent>
#include <QHeaderView>

GameTreeView::GameTreeView(QWidget* parent) : QTreeView(parent) {
    setFrameStyle(QFrame::NoFrame);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAllColumnsShowFocus(true);
    setUniformRowHeights(false);
    setSortingEnabled(true);
    setIndentation(20);
    setAlternatingRowColors(false);
    header()->setHighlightSections(false);
    header()->setStretchLastSection(true);
    header()->setSectionsMovable(true);
    ApplyTheme();

    connect(this, &QTreeView::activated, this, &GameTreeView::itemActivated);
}

void GameTreeView::ApplyTheme() {
    setStyleSheet(QStringLiteral(
        "QTreeView { background: transparent; border: none; outline: 0; }"
        "QTreeView::item { padding: 4px; border: none; }"
        "QTreeView::item:selected { background: rgba(255, 255, 255, 0.1); border-radius: 4px; }"
    ));
}

void GameTreeView::setModel(QAbstractItemModel* model) {
    QTreeView::setModel(model);
}

void GameTreeView::onNavigated(int dx, int dy) {
    if (!m_has_focus) return;
    auto* cur_model = model();
    if (!cur_model || cur_model->rowCount() == 0) return;

    QModelIndex current = currentIndex();
    if (!current.isValid()) {
        QModelIndex first = cur_model->index(0, 0);
        setCurrentIndex(first);
        scrollTo(first);
        return;
    }

    QAbstractItemView::CursorAction action;
    if (dy > 0) action = QAbstractItemView::MoveDown;
    else if (dy < 0) action = QAbstractItemView::MoveUp;
    else if (dx > 0) action = QAbstractItemView::MoveRight;
    else if (dx < 0) action = QAbstractItemView::MoveLeft;
    else return;

    QModelIndex next = moveCursor(action, Qt::NoModifier);
    if (next.isValid()) {
        setCurrentIndex(next);
        scrollTo(next);
    }
}

void GameTreeView::onActivated() {
    if (!m_has_focus) return;
    QModelIndex index = currentIndex();
    if (index.isValid()) {
        emit itemActivated(index);
    }
}

void GameTreeView::onCancelled() {}

void GameTreeView::mouseDoubleClickEvent(QMouseEvent* event) {
    QTreeView::mouseDoubleClickEvent(event);
}

void GameTreeView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QTreeView::selectionChanged(selected, deselected);
    if (!selected.indexes().isEmpty()) {
        emit itemSelectionChanged(selected.indexes().first());
    }
}
