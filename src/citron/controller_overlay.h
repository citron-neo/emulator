// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <QWidget>
#include <QTimer>

class GMainWindow;
class PlayerControlPreview;
class QSizeGrip;

class ControllerOverlay : public QWidget {
	Q_OBJECT

public:
	explicit ControllerOverlay(GMainWindow* parent);
	~ControllerOverlay() override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private slots:
	void UpdateControllerState();

private:
	GMainWindow* main_window;
	QTimer update_timer;

	PlayerControlPreview* controller_widget;
	QSizeGrip* size_grip;

	bool is_dragging = false;
	QPoint drag_start_pos;
};