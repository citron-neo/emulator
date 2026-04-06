#pragma once

#include <QWidget>
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

    void setModels(QAbstractItemModel* fav_model, QAbstractItemModel* main_model);
    void ApplyTheme();
    
    QListView* view() const { return m_main_view; }
    QListView* favView() const { return m_fav_view; }
    QAbstractItemModel* model() const { return mainModel(); }
    QItemSelectionModel* selectionModel() const;
    void setModel(QAbstractItemModel* model);
    
    QAbstractItemModel* favModel() const;
    QAbstractItemModel* mainModel() const;
    
    QRect visualRect(const QModelIndex& index) const;
    QWidget* viewport() const;
    void scrollTo(const QModelIndex& index);
    
    QModelIndex currentIndex() const;
    void setCurrentIndex(const QModelIndex& index);
    QModelIndex indexAt(const QPoint& p) const;

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
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void UpdateGridSize();
    void UpdateLayoutHeights();

    QScrollArea* m_scroll_area = nullptr;
    QWidget* m_container = nullptr;
    QListView* m_fav_view = nullptr;
    QListView* m_main_view = nullptr;
    QLabel* m_fav_label = nullptr;
    QLabel* m_main_label = nullptr;
    GameGridDelegate* m_fav_delegate = nullptr;
    GameGridDelegate* m_main_delegate = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_top_help = nullptr;
    QLabel* m_bottom_hint = nullptr;
    bool m_has_focus = false;
};

