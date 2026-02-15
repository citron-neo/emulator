// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QDialog>
#include "common/common_types.h"

namespace Ui {
class CustomMetadataDialog;
}

class QMovie;

class CustomMetadataDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomMetadataDialog(QWidget* parent, u64 program_id,
                                  const std::string& current_title);
    ~CustomMetadataDialog() override;

    [[nodiscard]] std::string GetTitle() const;
    [[nodiscard]] std::string GetIconPath() const;
    [[nodiscard]] bool WasReset() const;

private slots:
    void OnSelectIcon();

private:
    void UpdatePreview();

    std::unique_ptr<Ui::CustomMetadataDialog> ui;
    u64 program_id;
    std::string icon_path;
    QMovie* movie = nullptr;
    bool was_reset = false;
};
