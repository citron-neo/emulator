// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QWidget>
#include "common/common_types.h"

namespace Core {
class System;
}

/**
 * Simple ARM64 disassembler view - shows instruction bytes as hex.
 * CE-style: address, bytes, placeholder for disassembly.
 * Right-click: Go to address, Add breakpoint (placeholder).
 */
class DisassemblerViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit DisassemblerViewWidget(Core::System& system_, QWidget* parent = nullptr);
    ~DisassemblerViewWidget() override;

    void GotoAddress(u64 address);
    void OnEmulationStarting();
    void OnEmulationStopping();

signals:
    void GotoAddressRequested(u64 address);

private:
    void SetupUI();
    void RefreshView();
    void OnGotoPressed();
    void OnContextMenu(const QPoint& pos);

    Core::System& system;
    QLineEdit* address_input;
    QPlainTextEdit* disasm_view;
    u64 current_address{0};
};
