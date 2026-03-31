// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class SpinningLogo;
class QSequentialAnimationGroup;
class QVariantAnimation;

class GameListLoadingOverlay : public QWidget {
    Q_OBJECT

public:
    explicit GameListLoadingOverlay(QWidget* parent = nullptr);
    ~GameListLoadingOverlay() override;

    void SetStatusText(const QString& text);
    void ShowLoading();
    void ShowPopulated();
    void FadeOut();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void UpdateLayout();

    SpinningLogo* m_logo;
    QString m_current_status;
    bool m_is_fading_out = false;
};
