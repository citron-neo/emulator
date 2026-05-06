// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>

class QComboBox;
class QPushButton;
class QSlider;
class QLabel;

class ConfigureNeoThemes : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureNeoThemes(QWidget* parent = nullptr);
    ~ConfigureNeoThemes() override;

    void ApplyConfiguration();

private:
    void SetConfiguration();
    void OnSelectBG();
    void OnSelectColor(QPushButton* button, const QString& title, std::string& setting);

    QComboBox* ui_theme_combo{nullptr};

    QPushButton* button_bg_path{nullptr};
    QLabel* label_bg_path{nullptr};
    QSlider* slider_bg_opacity{nullptr};
    QPushButton* button_accent_color{nullptr};
    QPushButton* button_card_bg_color{nullptr};
    QPushButton* button_card_text_color{nullptr};
    QPushButton* button_card_dim_text_color{nullptr};
    QPushButton* button_card_outline_color{nullptr};
    QSlider* slider_card_outline_size{nullptr};
    QPushButton* button_selection_color{nullptr};
    QSlider* slider_selection_opacity{nullptr};
    QPushButton* button_list_bg_color{nullptr};
    QSlider* slider_card_opacity{nullptr};

    QPushButton* button_header_text_color{nullptr};
    QSlider* slider_header_opacity{nullptr};

    QPushButton* button_status_bar_text_color{nullptr};
    QPushButton* button_status_bar_bg_color{nullptr};
    QPushButton* button_status_bar_accent_color{nullptr};

    QPushButton* button_toolbar_text_color{nullptr};
    QPushButton* button_toolbar_bg_color{nullptr};

    QPushButton* button_header_bg_color{nullptr};

    class QCheckBox* checkbox_rainbow_mode{nullptr};

    std::string m_accent_color;
    std::string m_card_bg_color;
    std::string m_card_text_color;
    std::string m_card_dim_text_color;
    std::string m_card_outline_color;
    std::string m_selection_color;
    std::string m_list_bg_color;
    std::string m_header_text_color;
    std::string m_header_bg_color;
    std::string m_status_bar_text_color;
    std::string m_status_bar_bg_color;
    std::string m_status_bar_accent_color;
    std::string m_toolbar_text_color;
    std::string m_toolbar_bg_color;
    bool m_rainbow_mode{false};
};
