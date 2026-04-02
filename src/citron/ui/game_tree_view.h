#pragma once

#include <QTreeView>
#include <QHeaderView>
#include <QModelIndex>

class GameTreeView : public QTreeView {
    Q_OBJECT

public:
    explicit GameTreeView(QWidget* parent = nullptr);

    void ApplyTheme();
    void setModel(QAbstractItemModel* model) override;
    
    // Focus & Controller Navigation (Manual Implementation)
    void setControllerFocus(bool focus) { m_has_focus = focus; update(); }
    bool hasControllerFocus() const { return m_has_focus; }
    QWidget* view() { return this; }

public slots:
    void onNavigated(int dx, int dy);
    void onActivated();
    void onCancelled();

signals:
    void itemActivated(const QModelIndex& index);
    void itemSelectionChanged(const QModelIndex& index);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) override;

    bool m_has_focus = false;
};
