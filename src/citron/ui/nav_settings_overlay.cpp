#include <QGraphicsBlurEffect>
#include <QGraphicsColorizeEffect>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

#include "citron/ui/nav_settings_overlay.h"

NavigationSettingsOverlay::NavigationSettingsOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setupUI();
    hide();
}

void NavigationSettingsOverlay::setupUI() {
    m_container = new QWidget(this);
    m_container->setObjectName(QStringLiteral("glassContainer"));
    
    // Premium Glassmorphism Theme
    m_container->setStyleSheet(QStringLiteral(
        "QWidget#glassContainer {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
        "        stop:0 rgba(25, 25, 30, 0.9), "
        "        stop:1 rgba(15, 15, 20, 0.95));"
        "    border: 1px solid rgba(255, 255, 255, 0.12);"
        "    border-radius: 20px;"
        "}"
        "QLabel {"
        "    color: #f0f0f5;"
        "    font-weight: 500;"
        "}"
        "QLabel#title {"
        "    font-size: 20px;"
        "    font-weight: 900;"
        "    letter-spacing: 1px;"
        "    color: #fff;"
        "    margin-bottom: 5px;"
        "}"
        "QComboBox {"
        "    background: rgba(255, 255, 255, 0.05);"
        "    border: 1px solid rgba(255, 255, 255, 0.1);"
        "    border-radius: 8px;"
        "    padding: 5px 15px;"
        "    color: #fff;"
        "}"
        "QComboBox::drop-down {"
        "    border: none;"
        "}"
        "QPushButton {"
        "    background: rgba(255, 255, 255, 0.05);"
        "    border: 1px solid rgba(255, 255, 255, 0.1);"
        "    border-radius: 10px;"
        "    padding: 10px 20px;"
        "    color: #fff;"
        "    font-weight: bold;"
        "    font-size: 13px;"
        "}"
        "QPushButton#closeBtn {"
        "    background: #00d1ff; color: #000; border: none;"
        "}"
        "QPushButton:hover {"
        "    background: rgba(255, 255, 255, 0.1);"
        "    border-color: #00d1ff;"
        "}"
    ));

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(60);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 15);
    m_container->setGraphicsEffect(shadow);

    auto* main_layout = new QVBoxLayout(m_container);
    main_layout->setContentsMargins(30, 30, 30, 30);
    main_layout->setSpacing(25);

    auto* title_label = new QLabel(tr("NAVIGATION OPTIONS"), m_container);
    title_label->setObjectName(QStringLiteral("title"));
    main_layout->addWidget(title_label, 0, Qt::AlignCenter);

    auto* settings_grid = new QWidget(m_container);
    auto* grid_layout = new QGridLayout(settings_grid);
    grid_layout->setSpacing(20);

    auto add_setting = [&](const QString& label, QWidget* control, int row) {
        grid_layout->addWidget(new QLabel(label), row, 0);
        grid_layout->addWidget(control, row, 1, Qt::AlignRight);
    };

    auto* repeat_rate = new QComboBox(m_container);
    repeat_rate->addItems({tr("Slow"), tr("Normal"), tr("Fast")});
    repeat_rate->setCurrentIndex(1);
    add_setting(tr("Scroll Speed"), repeat_rate, 0);

    auto* deadzone = new QComboBox(m_container);
    deadzone->addItems({tr("Small"), tr("Medium"), tr("Large")});
    deadzone->setCurrentIndex(0);
    add_setting(tr("Stick Deadzone"), deadzone, 1);

    main_layout->addWidget(settings_grid);

    auto* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    auto* close_btn = new QPushButton(tr("DONE"), m_container);
    close_btn->setObjectName(QStringLiteral("closeBtn"));
    close_btn->setFixedWidth(120);
    connect(close_btn, &QPushButton::clicked, this, &NavigationSettingsOverlay::hideAnimated);
    btn_layout->addWidget(close_btn);
    btn_layout->addStretch();

    main_layout->addLayout(btn_layout);
}

void NavigationSettingsOverlay::showAnimated() {
    this->resize(parentWidget()->size());
    m_container->setFixedSize(420, 320);
    m_container->move((width() - m_container->width()) / 2, (height() - m_container->height()) / 2);

    show();
    
    // On-the-fly animations to avoid dangling pointers
    auto* fade_anim = new QPropertyAnimation(this, "windowOpacity", this);
    fade_anim->setDuration(400);
    fade_anim->setStartValue(0.0);
    fade_anim->setEndValue(1.0);
    fade_anim->setEasingCurve(QEasingCurve::OutQuint);

    auto* scale_anim = new QPropertyAnimation(m_container, "geometry", this);
    scale_anim->setDuration(400);
    QRect end_rect = m_container->geometry();
    QRect start_rect = end_rect.adjusted(20, 20, -20, -20);
    scale_anim->setStartValue(start_rect);
    scale_anim->setEndValue(end_rect);
    scale_anim->setEasingCurve(QEasingCurve::OutBack);
    
    auto* group = new QParallelAnimationGroup(this);
    group->addAnimation(fade_anim);
    group->addAnimation(scale_anim);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void NavigationSettingsOverlay::hideAnimated() {
    auto* fade_anim = new QPropertyAnimation(this, "windowOpacity", this);
    fade_anim->setDuration(300);
    fade_anim->setStartValue(windowOpacity());
    fade_anim->setEndValue(0.0);
    
    connect(fade_anim, &QPropertyAnimation::finished, this, &QWidget::hide);
    fade_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void NavigationSettingsOverlay::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 160)); // Deeper dim for focus
}
