// SPDX-FileCopyrightText: 2025 citron Emulator Project

#pragma once

#include <QProxyStyle>
#include <QTimer>
#include <QColor>

class RainbowStyle : public QProxyStyle {
    Q_OBJECT

public:
    explicit RainbowStyle(QStyle* baseStyle = nullptr);

    // This intercepts palette requests from every widget in the app
    QPalette standardPalette() const override;

    // A helper for widgets that need the color directly
    static QColor GetCurrentHighlightColor();

private slots:
    void UpdateHue();

private:
    QTimer* m_timer;
    static float s_hue;
};
