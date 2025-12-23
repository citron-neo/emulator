// SPDX-FileCopyrightText: 2025 citron Emulator Project

#include "citron/util/rainbow_style.h"
#include "citron/uisettings.h"
#include "citron/theme.h"
#include <QApplication>
#include <QColor>
#include <QTimer>

float RainbowStyle::s_hue = 0.0f;

RainbowStyle::RainbowStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &RainbowStyle::UpdateHue);
    m_timer->start(33);
}

void RainbowStyle::UpdateHue() {
    if (UISettings::values.enable_rainbow_mode.GetValue()) {
        s_hue += 0.005f;
        if (s_hue > 1.0f) s_hue = 0.0f;
    }
}

QColor RainbowStyle::GetCurrentHighlightColor() {
    if (!UISettings::values.enable_rainbow_mode.GetValue()) {
        return QColor(Theme::GetAccentColor());
    }
    return QColor::fromHsvF(s_hue, 0.7f, 1.0f);
}

QPalette RainbowStyle::standardPalette() const {
    QPalette pal = QProxyStyle::standardPalette();
    QColor highlight = GetCurrentHighlightColor();
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::Link, highlight);
    return pal;
}
