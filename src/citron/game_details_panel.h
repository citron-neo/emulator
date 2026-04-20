#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QGraphicsOpacityEffect>
#include <QVector>
#include <QFrame>
#include <QTimer>
#include <QModelIndex>
#include <QPersistentModelIndex>

#include "common/common_types.h"

class GameDetailsPanel : public QWidget {
    Q_OBJECT

public:
    explicit GameDetailsPanel(QWidget* parent = nullptr);
    ~GameDetailsPanel() override;

    void updateDetails(const QModelIndex& index);
    void ApplyTheme();

    void setControllerFocus(bool focus);
    bool hasControllerFocus() const { return m_has_focus; }

public slots:
    void onNavigated(int dx, int dy);
    void onActivated();
    void onCancelled();

signals:
    void actionTriggered(const QString& action_name, u64 program_id, const QString& path);
    void focusReturned();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void updateStyles();
    bool IsDarkMode() const;
    void applyDetails(const QModelIndex& index);
    void clearActions();
    void addAction(const QString& label, const QString& action_id);

    QLabel* m_icon_label;
    QLabel* m_title_label;
    QLabel* m_id_label;
    QFrame* m_title_card;
    QFrame* m_meta_card;
    QWidget* m_actions_container;
    QVBoxLayout* m_actions_layout;
    QScrollArea* m_scroll_area;
    QGraphicsOpacityEffect* m_opacity_effect;

    u64 m_current_program_id = 0;
    QString m_current_path;

    QList<QPushButton*> m_action_buttons;
    int m_focused_button_index = -1;
    bool m_has_focus = false;
    QTimer* m_debounce_timer;
    QPersistentModelIndex m_pending_index;
};
