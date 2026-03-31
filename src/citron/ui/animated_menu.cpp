#include "animated_menu.h"
#include <QShowEvent>

AnimatedMenu::AnimatedMenu(QWidget* parent) : QMenu(parent) {
    InitAnimations();
}

AnimatedMenu::AnimatedMenu(const QString& title, QWidget* parent) : QMenu(title, parent) {
    InitAnimations();
}

void AnimatedMenu::InitAnimations() {
    setWindowFlags(windowFlags() | Qt::NoDropShadowWindowHint); // Optional: if you want a custom flat look

    opacity_anim = new QPropertyAnimation(this, "windowOpacity");
    opacity_anim->setDuration(200);
    opacity_anim->setStartValue(0.0);
    opacity_anim->setEndValue(1.0);
    opacity_anim->setEasingCurve(QEasingCurve::OutCubic);

    geometry_anim = new QPropertyAnimation(this, "geometry");
    geometry_anim->setDuration(200);
    geometry_anim->setEasingCurve(QEasingCurve::OutBack);

    show_anim_group = new QParallelAnimationGroup(this);
    show_anim_group->addAnimation(opacity_anim);
    show_anim_group->addAnimation(geometry_anim);
}

void AnimatedMenu::showEvent(QShowEvent* event) {
    QMenu::showEvent(event);

    if (show_anim_group->state() == QAbstractAnimation::Running) {
        show_anim_group->stop();
    }

    // Set starting geometry slightly above to create a slide-down effect
    QRect finalGeometry = geometry();
    QRect startGeometry = finalGeometry;
    startGeometry.translate(0, -10);

    geometry_anim->setStartValue(startGeometry);
    geometry_anim->setEndValue(finalGeometry);

    setWindowOpacity(0.0);
    show_anim_group->start();
}
