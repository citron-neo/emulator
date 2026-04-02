#include <QMouseEvent>
#include <QItemSelection>
#include <QAbstractItemView>
#include "citron/ui/game_view_base.h"

GameViewBase::GameViewBase(QWidget* parent) : QListView(parent) {
    setFrameStyle(QFrame::NoFrame);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setMouseTracking(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}

GameViewBase::~GameViewBase() = default;

void GameViewBase::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        emit itemActivated(index);
    }
    QListView::mouseDoubleClickEvent(event);
}

void GameViewBase::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QListView::selectionChanged(selected, deselected);
    if (!selected.indexes().isEmpty()) {
        emit itemSelectionChanged(selected.indexes().first());
    }
}

void GameViewBase::onNavigated(int dx, int dy) {
    if (!m_has_focus) return;

    auto* cur_model = model();
    if (!cur_model || cur_model->rowCount() == 0) return;

    QModelIndex current = currentIndex();
    if (!current.isValid()) {
        current = cur_model->index(0, 0);
        setCurrentIndex(current);
        scrollTo(current);
        return;
    }

    QAbstractItemView::CursorAction action;
    if (dx > 0) action = QAbstractItemView::MoveRight;
    else if (dx < 0) action = QAbstractItemView::MoveLeft;
    else if (dy > 0) action = QAbstractItemView::MoveDown;
    else if (dy < 0) action = QAbstractItemView::MoveUp;
    else return;

    QModelIndex next = moveCursor(action, Qt::NoModifier);
    if (next.isValid()) {
        setCurrentIndex(next);
        scrollTo(next);
    }
}

void GameViewBase::onActivated() {
    if (!m_has_focus) return;
    QModelIndex index = currentIndex();
    if (index.isValid()) {
        emit itemActivated(index);
    }
}

void GameViewBase::onCancelled() {
    // Basic views don't have a default cancellation behavior yet
}
