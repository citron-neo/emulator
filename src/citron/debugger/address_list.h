// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <vector>
#include "common/common_types.h"

namespace Core {
class System;
}

/**
 * Address List / Cheat Table - CE-style address list with freeze, edit, save/load.
 * Columns: Address, Type, Value, Frozen, Description
 * Save/load .ct JSON format.
 */
class AddressListWidget : public QWidget {
    Q_OBJECT

public:
    explicit AddressListWidget(Core::System& system_, QWidget* parent = nullptr);
    ~AddressListWidget() override;

    void OnEmulationStarting();
    void OnEmulationStopping();

    /// Add address to list (called from memory viewer).
    void AddAddress(u64 address, int type_size = 4);

signals:
    void GotoAddressRequested(u64 address);

private:
    void SetupUI();
    void UpdateTable();
    void OnAddAddress();
    void OnRemoveAddress();
    void OnToggleFreeze(int row);
    void OnEditValue(int row, int column);
    void OnContextMenu(const QPoint& pos);
    void OnPointerScan();
    void OnSaveTable();
    void OnLoadTable();
    void ApplyFrozenValues();

    enum class ValueType : int { U8 = 0, U16 = 1, U32 = 2, U64 = 3, Float = 4, Double = 5 };
    struct CheatEntry {
        u64 address;
        ValueType type;
        bool frozen;
        std::vector<u8> frozen_value;
        QString description;
    };
    std::vector<CheatEntry> entries;

    Core::System& system;
    QTableWidget* table;
    QPushButton* add_btn;
    QPushButton* remove_btn;
    QPushButton* save_btn;
    QPushButton* load_btn;
    QPushButton* pointer_scan_btn;
};
