// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QTableWidget>
#include <atomic>
#include <future>
#include <mutex>
#include <optional>
#include <vector>
#include "common/common_types.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

/**
 * Memory Viewer Widget - Cheat Engine-style memory browser integrated into Citron.
 *
 * Features:
 * - Live hex view with ASCII sidebar (16 bytes per row)
 * - Browse Switch process memory (heap, main NSO, alias, code regions)
 * - Search for u8/u16/u32/u64/float/string
 * - Add/freeze addresses to watch list
 * - Right-click "Find what writes/reads" (sets hardware watchpoint)
 * - Thread-safe background scanning
 */
class MemoryViewerWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit MemoryViewerWidget(Core::System& system_, QWidget* parent = nullptr);
    ~MemoryViewerWidget() override;

    /// Returns a QAction that toggles visibility of this dock.
    QAction* toggleViewAction();

    /// Jump to and display the given virtual address.
    void GotoAddress(u64 address);

    /// Called when emulation starts (enable UI).
    void OnEmulationStarting();
    /// Called when emulation stops (disable UI, clear volatile state).
    void OnEmulationStopping();

signals:
    void AddressSelectedForDisassembly(u64 address);
    void AddToAddressListRequested(u64 address, int size);

private:
    void SetupUI();
    void RefreshMemoryView();
    void UpdateWatchList();
    void DoSearch();
    void OnSearchComplete();
    void OnSearchProgressTick();  // Timer: update progress bar on main thread
    void OnMemoryRegionChanged(int index);
    void OnAddressInputReturnPressed();
    void OnWatchListContextMenu(const QPoint& pos);
    void OnHexViewContextMenu(const QPoint& pos);
    void AddToWatchList(u64 address, int size);
    void RemoveFromWatchList(int row);
    void ToggleWatchFreeze(int row);
    void SetWatchpoint(u64 address, bool read, bool write);
    void RemoveWatchpoint(u64 address);
    void FreezeWatchValue(int row);
    void UnfreezeWatchValue(int row);

    std::vector<u8> ReadMemoryBlock(u64 address, size_t size);
    bool WriteMemoryBlock(u64 address, const void* data, size_t size);
    bool IsAddressValid(u64 address) const;
    std::optional<u64> ParseAddress(const QString& text) const;

    Core::System& system;
    Kernel::KProcess* current_process{};

    // Memory regions (name, base, size)
    struct MemoryRegion {
        QString name;
        u64 base;
        u64 size;
    };
    std::vector<MemoryRegion> regions;
    int current_region_index{0};

    // UI
    QWidget* container;
    QLineEdit* address_input;
    QLineEdit* search_input;
    QComboBox* region_combo;
    QComboBox* search_type_combo;
    QPushButton* search_button;
    QPlainTextEdit* hex_view;
    QTableWidget* watch_table;
    QProgressBar* search_progress;
    QTimer* search_progress_timer{};
    QLabel* status_label;

    // Watch list: address, size (1/2/4/8), frozen, frozen_value_bytes
    struct WatchEntry {
        u64 address;
        int size;  // 1, 2, 4, 8
        bool frozen;
        std::vector<u8> frozen_value;
    };
    std::vector<WatchEntry> watch_entries;
    std::mutex watch_mutex;

    // Search state (background thread)
    std::atomic<bool> search_running{false};
    std::atomic<int> search_progress_pct{0};  // Worker writes, timer reads on main thread
    std::future<void> search_future;
    std::vector<u64> search_results;
    QString search_error;  // FIXED: float scan crash - error message from scan thread
    std::mutex search_mutex;
    u64 search_start_addr{};
    u64 search_end_addr{};
    std::vector<u8> search_value;
    std::vector<bool> search_aob_mask;  // true = wildcard for AOB
    int search_value_size{4};
    enum class SearchType { U8, U16, U32, U64, S8, S16, S32, S64, Float, String, AOB };
    SearchType current_search_type{SearchType::U32};

    static constexpr size_t BYTES_PER_ROW = 16;
    static constexpr size_t VIEW_SIZE = 4096;  // bytes to display
    static constexpr size_t MAX_SEARCH_RESULTS = 10000;
    static constexpr float FLOAT_SEARCH_EPSILON = 0.001f;  // FIXED: UltraCam 1.9 float tolerance
};
