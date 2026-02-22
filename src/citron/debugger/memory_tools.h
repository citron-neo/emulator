// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDockWidget>
#include <QTabWidget>

namespace Core {
class System;
}

class MemoryViewerWidget;
class FunctionBrowserWidget;
class AddressListWidget;
class DisassemblerViewWidget;

/**
 * CE-style Memory & Cheat Engine tool - Memory Viewer, Address List, Functions, Disassembly.
 * Access via Tools → Memory & Cheat Engine or Ctrl+Shift+M.
 */
class MemoryToolsWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit MemoryToolsWidget(Core::System& system_, QWidget* parent = nullptr);
    ~MemoryToolsWidget() override;

    QAction* toggleViewAction();

    void OnEmulationStarting();
    void OnEmulationStopping();

private:
    void OnFunctionAddressSelected(u64 address);
    void OnGotoAddress(u64 address);

    QTabWidget* tabs;
    MemoryViewerWidget* memory_viewer;
    FunctionBrowserWidget* function_browser;
    AddressListWidget* address_list;
    DisassemblerViewWidget* disassembler_view;
};
