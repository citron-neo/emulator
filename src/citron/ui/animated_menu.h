#pragma once

#include <QMenu>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>

class AnimatedMenu : public QMenu {
    Q_OBJECT

public:
    explicit AnimatedMenu(QWidget* parent = nullptr);
    explicit AnimatedMenu(const QString& title, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void InitAnimations();

    QPropertyAnimation* opacity_anim;
    QPropertyAnimation* geometry_anim;
    QParallelAnimationGroup* show_anim_group;
};
