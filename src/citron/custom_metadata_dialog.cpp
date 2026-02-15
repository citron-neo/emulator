// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFileDialog>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include "citron/custom_metadata.h"
#include "citron/custom_metadata_dialog.h"
#include "ui_custom_metadata_dialog.h"

CustomMetadataDialog::CustomMetadataDialog(QWidget* parent, u64 program_id_,
                                           const std::string& current_title)
    : QDialog(parent), ui(std::make_unique<Ui::CustomMetadataDialog>()), program_id(program_id_) {
    ui->setupUi(this);
    ui->title_edit->setText(QString::fromStdString(current_title));

    if (auto current_icon_path =
            Citron::CustomMetadata::GetInstance().GetCustomIconPath(program_id)) {
        icon_path = *current_icon_path;
        UpdatePreview();
    }

    connect(ui->select_icon_button, &QPushButton::clicked, this,
            &CustomMetadataDialog::OnSelectIcon);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CustomMetadataDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &CustomMetadataDialog::reject);
    connect(ui->buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        was_reset = true;
        accept();
    });
}

CustomMetadataDialog::~CustomMetadataDialog() = default;

std::string CustomMetadataDialog::GetTitle() const {
    return ui->title_edit->text().toStdString();
}

std::string CustomMetadataDialog::GetIconPath() const {
    return icon_path;
}

bool CustomMetadataDialog::WasReset() const {
    return was_reset;
}

void CustomMetadataDialog::OnSelectIcon() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Select Icon"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.gif)"));

    if (!path.isEmpty()) {
        icon_path = path.toStdString();
        UpdatePreview();
    }
}

void CustomMetadataDialog::UpdatePreview() {
    if (movie) {
        movie->stop();
        delete movie;
        movie = nullptr;
    }

    if (icon_path.empty()) {
        ui->icon_preview->setPixmap(QPixmap());
        return;
    }

    const QString qpath = QString::fromStdString(icon_path);
    if (qpath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
        movie = new QMovie(qpath, QByteArray(), this);
        if (movie->isValid()) {
            ui->icon_preview->setMovie(movie);
            movie->start();
        }
    } else {
        QPixmap pixmap(qpath);
        if (!pixmap.isNull()) {
            QPixmap rounded(pixmap.size());
            rounded.fill(Qt::transparent);
            QPainter painter(&rounded);
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            const int radius = pixmap.width() / 6;
            path.addRoundedRect(rounded.rect(), radius, radius);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, pixmap);

            ui->icon_preview->setPixmap(rounded.scaled(
                ui->icon_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}
