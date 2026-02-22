// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <map>
#include <vector>
#include "common/common_types.h"

namespace Core {
class System;
}

/**
 * Function Browser Widget - List and manage function symbols with Ghidra CSV sync.
 *
 * Features:
 * - List loaded functions/modules (from NSO/NRO symbols or manual import)
 * - Columns: Address, Name, Size, Module
 * - Import function names from Ghidra CSV export (address,name format)
 * - Export current function list to Ghidra-compatible CSV
 * - Double-click function → jump to address in memory view + show disassembly
 */
class FunctionBrowserWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit FunctionBrowserWidget(Core::System& system_, QWidget* parent = nullptr);
    ~FunctionBrowserWidget() override;

    QAction* toggleViewAction();

    /// Refresh the function list from current process modules/symbols.
    void RefreshFunctions();

    /// Jump to the given address in memory view (emit signal for parent to handle).
    void GotoAddress(u64 address);

    /// Called when emulation starts.
    void OnEmulationStarting();
    /// Called when emulation stops.
    void OnEmulationStopping();

signals:
    void AddressSelected(u64 address);

private:
    void SetupUI();
    void OnTableDoubleClicked(int row, int column);
    void OnImportGhidraCsv();
    void OnExportGhidraCsv();
    void OnFilterTextChanged(const QString& text);
    void LoadFromModules();

    struct FunctionEntry {
        u64 address;
        std::string name;
        u64 size;
        std::string module;
    };
    std::vector<FunctionEntry> functions;
    std::vector<FunctionEntry> filtered_functions;
    std::map<u64, std::string> ghidra_import_overrides;  // Address -> name from imported CSV

    Core::System& system;

    QTableWidget* table;
    QLineEdit* filter_input;
};
