#pragma once

#include <QListView>
#include <QVBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QModelIndex>
#include <QRect>
#include <QPoint>

#include "citron/game_grid_delegate.h"

class QAbstractItemModel;
class QItemSelectionModel;

class GameGridView : public QWidget {
    Q_OBJECT

public:
    explicit GameGridView(QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model);
    void ApplyTheme();
    
    // Accessors for compatibility
    QListView* view() const { return m_view; }
    QAbstractItemModel* model() const;
    QItemSelectionModel* selectionModel() const;
    QRect visualRect(const QModelIndex& index) const;
    QWidget* viewport() const;
    void scrollTo(const QModelIndex& index);
    
    // Forwarded from GameViewBase equivalents
    QModelIndex currentIndex() const { return m_view->currentIndex(); }
    void setCurrentIndex(const QModelIndex& index) { m_view->setCurrentIndex(index); }
    QModelIndex indexAt(const QPoint& p) const { return m_view->indexAt(p); }

    // Controller Navigation
    void setControllerFocus(bool focus);
    bool hasControllerFocus() const { return m_has_focus; }

public slots:
    void onNavigated(int dx, int dy);
    void onActivated();
    void onCancelled();

signals:
    void itemActivated(const QModelIndex& index);
    void itemSelectionChanged(const QModelIndex& index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QListView* m_view = nullptr;
    GameGridDelegate* m_delegate = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_top_help = nullptr;
    QLabel* m_bottom_hint = nullptr;
    bool m_has_focus = false;
};
