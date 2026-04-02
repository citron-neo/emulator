#pragma once

#include <QListView>
#include <QModelIndex>
#include <QStandardItemModel>

class GameViewBase : public QListView {
    Q_OBJECT

public:
    explicit GameViewBase(QWidget* parent = nullptr);
    virtual ~GameViewBase() override;

    virtual void ApplyTheme() = 0;
    virtual void filterItems(const QString& text) {}

    // Focus & Controller Navigation
    virtual void setControllerFocus(bool focus) { m_has_focus = focus; update(); }
    bool hasControllerFocus() const { return m_has_focus; }

public slots:
    virtual void onNavigated(int dx, int dy);
    virtual void onActivated();
    virtual void onCancelled();

signals:
    void itemActivated(const QModelIndex& index);
    void itemSelectionChanged(const QModelIndex& index);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) override;

    bool m_has_focus = false;
};
