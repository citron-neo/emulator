#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>

class NavigationSettingsOverlay : public QWidget {
    Q_OBJECT

public:
    explicit NavigationSettingsOverlay(QWidget* parent = nullptr);

    void showAnimated();
    void hideAnimated();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    
    QWidget* m_container;
    QVBoxLayout* m_layout;
    QPropertyAnimation* m_animation;
};
