// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/debugger/memory_viewer.h"
#include <QApplication>
#include <QByteArray>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QShortcut>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/hex_util.h"
#include "common/swap.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "common/typed_address.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_types.h"
#include "common/logging.h"
#include "core/memory.h"
#include "core/memory/dmnt_cheat_types.h"

namespace {

constexpr int NUM_WATCHPOINTS = 4;  // Switch Cortex-A57 hardware limit

QString MemoryViewFormatAddress(u64 addr) {
    return QString::asprintf("0x%016llX", static_cast<unsigned long long>(addr));
}

} // namespace

MemoryViewerWidget::MemoryViewerWidget(Core::System& system_, QWidget* parent)
    : QDockWidget(parent), system(system_) {
    setObjectName(QStringLiteral("MemoryViewer"));
    setWindowTitle(tr("Memory & Functions - Memory Viewer"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea |
                    Qt::BottomDockWidgetArea);

    SetupUI();

    // Refresh timer for live memory view and watch list
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MemoryViewerWidget::RefreshMemoryView);
    timer->start(100);  // 10 Hz refresh when visible
}

MemoryViewerWidget::~MemoryViewerWidget() = default;

QAction* MemoryViewerWidget::toggleViewAction() {
    return QDockWidget::toggleViewAction();
}

void MemoryViewerWidget::SetupUI() {
    container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);

    // Top bar: address, region, goto
    auto* top_bar = new QHBoxLayout();
    top_bar->addWidget(new QLabel(tr("Address:")));
    address_input = new QLineEdit(this);
    address_input->setPlaceholderText(tr("0x7100000000"));
    address_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    address_input->setMinimumWidth(180);
    connect(address_input, &QLineEdit::returnPressed, this,
            &MemoryViewerWidget::OnAddressInputReturnPressed);
    top_bar->addWidget(address_input);
    auto* goto_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    connect(goto_shortcut, &QShortcut::activated, this, [this]() {
        address_input->setFocus();
        address_input->selectAll();
    });

    top_bar->addWidget(new QLabel(tr("Region:")));
    region_combo = new QComboBox(this);
    region_combo->setMinimumWidth(140);
    connect(region_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MemoryViewerWidget::OnMemoryRegionChanged);
    top_bar->addWidget(region_combo);

    top_bar->addStretch();
    layout->addLayout(top_bar);

    // Hex view
    hex_view = new QPlainTextEdit(this);
    hex_view->setReadOnly(true);
    hex_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    hex_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    hex_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hex_view, &QPlainTextEdit::customContextMenuRequested, this,
            &MemoryViewerWidget::OnHexViewContextMenu);
    layout->addWidget(hex_view, 1);

    // Search bar
    auto* search_bar = new QHBoxLayout();
    search_bar->addWidget(new QLabel(tr("Search:")));
    search_input = new QLineEdit(this);
    search_input->setPlaceholderText(tr("e.g. 1.9, 0x3F, 42 (hex, decimal, or float)"));
    search_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    search_bar->addWidget(search_input, 1);

    search_type_combo = new QComboBox(this);
    search_type_combo->addItem(tr("u8"), static_cast<int>(SearchType::U8));
    search_type_combo->addItem(tr("u16"), static_cast<int>(SearchType::U16));
    search_type_combo->addItem(tr("u32"), static_cast<int>(SearchType::U32));
    search_type_combo->addItem(tr("u64"), static_cast<int>(SearchType::U64));
    search_type_combo->addItem(tr("s8"), static_cast<int>(SearchType::S8));
    search_type_combo->addItem(tr("s16"), static_cast<int>(SearchType::S16));
    search_type_combo->addItem(tr("s32"), static_cast<int>(SearchType::S32));
    search_type_combo->addItem(tr("s64"), static_cast<int>(SearchType::S64));
    search_type_combo->addItem(tr("float"), static_cast<int>(SearchType::Float));
    search_type_combo->addItem(tr("string"), static_cast<int>(SearchType::String));
    search_type_combo->addItem(tr("AOB"), static_cast<int>(SearchType::AOB));
    search_type_combo->setCurrentIndex(2);  // u32 default
    search_bar->addWidget(search_type_combo);

    search_button = new QPushButton(tr("Search"), this);
    connect(search_button, &QPushButton::clicked, this, &MemoryViewerWidget::DoSearch);
    search_bar->addWidget(search_button);

    search_progress = new QProgressBar(this);
    search_progress->setRange(0, 100);
    search_progress->setVisible(false);
    search_bar->addWidget(search_progress);

    search_progress_timer = new QTimer(this);
    search_progress_timer->setInterval(100);
    connect(search_progress_timer, &QTimer::timeout, this, &MemoryViewerWidget::OnSearchProgressTick);

    layout->addLayout(search_bar);

    // Watch list
    auto* watch_group = new QGroupBox(tr("Watch List"));
    auto* watch_layout = new QVBoxLayout(watch_group);
    watch_table = new QTableWidget(this);
    watch_table->setColumnCount(5);
    watch_table->setHorizontalHeaderLabels(
        {tr("Address"), tr("Size"), tr("Value"), tr("Frozen"), tr("Description")});
    watch_table->horizontalHeader()->setStretchLastSection(true);
    watch_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(watch_table, &QTableWidget::customContextMenuRequested, this,
            &MemoryViewerWidget::OnWatchListContextMenu);
    watch_layout->addWidget(watch_table);
    layout->addWidget(watch_group);

    status_label = new QLabel(tr("No game running"), this);
    layout->addWidget(status_label);

    setWidget(container);
}

void MemoryViewerWidget::RefreshMemoryView() {
    if (!isVisible()) {
        return;
    }

    auto* process = system.ApplicationProcess();
    if (!process) {
        status_label->setText(tr("No game running"));
        return;
    }

    current_process = process;

    // Build regions on first refresh after process available
    if (regions.empty()) {
        const auto& page_table = process->GetPageTable();
        regions.clear();
        u64 heap_base = GetInteger(page_table.GetHeapRegionStart());
        u64 heap_size = page_table.GetHeapRegionSize();
        if (heap_size > 0) {
            regions.push_back({tr("Heap"), heap_base, heap_size});
        }
        u64 alias_base = GetInteger(page_table.GetAliasRegionStart());
        u64 alias_size = page_table.GetAliasRegionSize();
        if (alias_size > 0) {
            regions.push_back({tr("Alias"), alias_base, alias_size});
        }
        u64 code_base = GetInteger(page_table.GetAliasCodeRegionStart());
        u64 code_size = page_table.GetAliasCodeRegionSize();
        if (code_size > 0) {
            regions.push_back({tr("Code (main)"), code_base, code_size});
        }
        // Add generic 64-bit user space range for manual browsing
        regions.push_back({tr("Full (0x0-0x7FFFFFFFFFFF)"), 0, 0x800000000000ULL});

        region_combo->blockSignals(true);
        region_combo->clear();
        for (const auto& r : regions) {
            region_combo->addItem(r.name);
        }
        region_combo->blockSignals(false);
        current_region_index = 0;
        if (!regions.empty()) {
            region_combo->setCurrentIndex(0);
        }
    }

    status_label->setText(tr("Process 0x%1").arg(process->GetProcessId(), 8, 16, QLatin1Char('0')));

    // Get current view address
    auto addr_opt = ParseAddress(address_input->text());
    u64 view_addr = addr_opt.value_or(0);
    if (!addr_opt && !regions.empty() && current_region_index < static_cast<int>(regions.size())) {
        view_addr = regions[current_region_index].base;
    }

    if (!IsAddressValid(view_addr)) {
        return;
    }

    // Read memory
    auto mem_block = ReadMemoryBlock(view_addr, VIEW_SIZE);
    if (mem_block.empty()) {
        hex_view->setPlainText(tr("(Unable to read memory at 0x%1)").arg(view_addr, 16, 16));
        return;
    }

    // Build hex view: "0x7100000000: 00 11 22 33 ...  |....|"
    QString out;
    for (size_t i = 0; i < mem_block.size(); i += BYTES_PER_ROW) {
        u64 line_addr = view_addr + i;
        out += MemoryViewFormatAddress(line_addr) + QStringLiteral(": ");
        QString hex_part;
        QString ascii_part;
        for (size_t j = 0; j < BYTES_PER_ROW && (i + j) < mem_block.size(); ++j) {
            u8 b = mem_block[i + j];
            hex_part += QString::asprintf("%02X ", b);
            ascii_part += (b >= 32 && b < 127) ? QLatin1Char(static_cast<char>(b)) : QLatin1Char('.');
        }
        out += hex_part.leftJustified(BYTES_PER_ROW * 3) + QStringLiteral(" |") + ascii_part +
               QStringLiteral("|\n");
    }
    hex_view->setPlainText(out);

    // Update watch list
    UpdateWatchList();
}

void MemoryViewerWidget::UpdateWatchList() {
    std::lock_guard lock(watch_mutex);
    watch_table->setRowCount(static_cast<int>(watch_entries.size()));

    auto* mem = system.ApplicationProcess() ? &system.ApplicationProcess()->GetMemory() : nullptr;
    if (!mem) {
        return;
    }

    for (size_t i = 0; i < watch_entries.size(); ++i) {
        const auto& e = watch_entries[i];
        watch_table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(MemoryViewFormatAddress(e.address)));
        watch_table->setItem(static_cast<int>(i), 1,
                             new QTableWidgetItem(QString::number(e.size)));

        QString value_str;
        if (e.frozen && !e.frozen_value.empty()) {
            if (e.size == 1) {
                value_str = QString::asprintf("0x%02X", e.frozen_value[0]);
            } else if (e.size == 2) {
                u16 v;
                std::memcpy(&v, e.frozen_value.data(), 2);
                value_str = QString::asprintf("0x%04X", Common::swap16(v));
            } else if (e.size == 4) {
                u32 v;
                std::memcpy(&v, e.frozen_value.data(), 4);
                value_str = QString::asprintf("0x%08X", Common::swap32(v));
            } else if (e.size == 8) {
                u64 v;
                std::memcpy(&v, e.frozen_value.data(), 8);
                value_str = QString::asprintf("0x%016llX", static_cast<unsigned long long>(
                                                             Common::swap64(v)));
            }
        } else if (IsAddressValid(e.address)) {
            try {
                if (e.size == 1) {
                    u8 v = mem->Read8(Common::ProcessAddress(e.address));
                    value_str = QString::asprintf("0x%02X", v);
                } else if (e.size == 2) {
                    u16 v = mem->Read16(Common::ProcessAddress(e.address));
                    value_str = QString::asprintf("0x%04X", v);
                } else if (e.size == 4) {
                    u32 v = mem->Read32(Common::ProcessAddress(e.address));
                    value_str = QString::asprintf("0x%08X", v);
                } else if (e.size == 8) {
                    u64 v = mem->Read64(Common::ProcessAddress(e.address));
                    value_str = QString::asprintf("0x%016llX",
                                                 static_cast<unsigned long long>(v));
                }
            } catch (...) {
                value_str = tr("(read error)");
            }
        } else {
            value_str = tr("(invalid)");
        }

        watch_table->setItem(static_cast<int>(i), 2, new QTableWidgetItem(value_str));
        auto* freeze_item = new QTableWidgetItem(e.frozen ? tr("Yes") : tr("No"));
        freeze_item->setFlags(freeze_item->flags() | Qt::ItemIsUserCheckable);
        freeze_item->setCheckState(e.frozen ? Qt::Checked : Qt::Unchecked);
        watch_table->setItem(static_cast<int>(i), 3, freeze_item);
        watch_table->setItem(static_cast<int>(i), 4, new QTableWidgetItem(QString()));
    }

    // Apply frozen values
    for (size_t i = 0; i < watch_entries.size(); ++i) {
        const auto& e = watch_entries[i];
        if (e.frozen && !e.frozen_value.empty() && IsAddressValid(e.address)) {
            mem->WriteBlock(Common::ProcessAddress(e.address), e.frozen_value.data(),
                            e.frozen_value.size());
        }
    }
}

void MemoryViewerWidget::DoSearch() {
    if (search_running) {
        return;
    }

    auto* process = system.ApplicationProcess();
    if (!process) {
        QMessageBox::warning(this, tr("Search"), tr("No game running."));
        return;
    }

    u64 start_addr = 0;
    u64 end_addr = 0;
    if (current_region_index < static_cast<int>(regions.size())) {
        const auto& r = regions[current_region_index];
        start_addr = r.base;
        end_addr = (r.size > 0) ? (r.base + r.size) : 0x800000000000ULL;
    }

    if (start_addr >= end_addr) {
        QMessageBox::warning(this, tr("Search"), tr("Invalid search range."));
        return;
    }

    // Parse search value
    QString search_text = search_input->text().trimmed();
    search_value.clear();
    current_search_type = static_cast<SearchType>(search_type_combo->currentData().toInt());

    bool parse_ok = false;
    search_aob_mask.clear();
    if (current_search_type == SearchType::AOB) {
        // Parse hex pattern: "48 8B 05 ?? ?? ?? ??" - ?? = wildcard
        QStringList parts = search_text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            if (p == QLatin1String("??") || p == QLatin1String("?")) {
                search_value.push_back(0);
                search_aob_mask.push_back(true);
            } else {
                bool ok;
                int v = p.toInt(&ok, 16);
                if (ok && v >= 0 && v <= 255) {
                    search_value.push_back(static_cast<u8>(v));
                    search_aob_mask.push_back(false);
                }
            }
        }
        parse_ok = !search_value.empty();
        if (parse_ok) search_value_size = static_cast<int>(search_value.size());
    } else if (current_search_type == SearchType::String) {
        const QByteArray ba = search_text.toUtf8();
        search_value.assign(ba.begin(), ba.end());
        parse_ok = !search_value.empty();
    } else if (current_search_type == SearchType::Float) {
        // Parse float first - must not go through toULongLong (1.9 would fail)
        // Do NOT swap: Switch is little-endian, host is usually LE - store raw bytes
        float f = search_text.toFloat(&parse_ok);
        if (parse_ok) {
            search_value.resize(4);
            std::memcpy(search_value.data(), &f, 4);
            search_value_size = 4;
        }
    } else {
        bool hex = search_text.startsWith(QStringLiteral("0x"));
        if (current_search_type == SearchType::S8 || current_search_type == SearchType::S16 ||
            current_search_type == SearchType::S32 || current_search_type == SearchType::S64) {
            qlonglong sval = search_text.toLongLong(&parse_ok, hex ? 16 : 10);
            if (parse_ok) {
                if (current_search_type == SearchType::S8) {
                    search_value.push_back(
                        static_cast<u8>(static_cast<s8>(sval)));
                    search_value_size = 1;
                } else if (current_search_type == SearchType::S16) {
                    u16 v = Common::swap16(static_cast<u16>(static_cast<s16>(sval)));
                    search_value.resize(2);
                    std::memcpy(search_value.data(), &v, 2);
                    search_value_size = 2;
                } else if (current_search_type == SearchType::S32) {
                    u32 v = Common::swap32(static_cast<u32>(static_cast<s32>(sval)));
                    search_value.resize(4);
                    std::memcpy(search_value.data(), &v, 4);
                    search_value_size = 4;
                } else {
                    u64 v = Common::swap64(static_cast<u64>(static_cast<s64>(sval)));
                    search_value.resize(8);
                    std::memcpy(search_value.data(), &v, 8);
                    search_value_size = 8;
                }
            }
        } else {
            qulonglong val = search_text.toULongLong(&parse_ok, hex ? 16 : 10);
            if (parse_ok) {
                if (current_search_type == SearchType::U8) {
                    search_value.push_back(static_cast<u8>(val & 0xFF));
                    search_value_size = 1;
                } else if (current_search_type == SearchType::U16) {
                    u16 v = static_cast<u16>(val & 0xFFFF);
                    v = Common::swap16(v);
                    search_value.resize(2);
                    std::memcpy(search_value.data(), &v, 2);
                    search_value_size = 2;
                } else if (current_search_type == SearchType::U32) {
                    u32 v = static_cast<u32>(val);
                    v = Common::swap32(v);
                    search_value.resize(4);
                    std::memcpy(search_value.data(), &v, 4);
                    search_value_size = 4;
                } else if (current_search_type == SearchType::U64) {
                    u64 v = val;
                    v = Common::swap64(v);
                    search_value.resize(8);
                    std::memcpy(search_value.data(), &v, 8);
                    search_value_size = 8;
                }
            }
        }
    }

    if (!parse_ok || search_value.empty()) {
        QMessageBox::warning(this, tr("Search"), tr("Invalid search value."));
        return;
    }

    // FIXED: clamp scan range to avoid multi-minute freeze / Not Responding
    constexpr u64 SCAN_MAX_BYTES = 0x80000000ULL;  // 2GB max - completes in ~10–30 sec
    if (end_addr - start_addr > SCAN_MAX_BYTES) {
        end_addr = start_addr + SCAN_MAX_BYTES;
    }
    if (start_addr >= end_addr) {
        QMessageBox::warning(this, tr("Search"), tr("Invalid search range."));
        return;
    }

    search_start_addr = start_addr;
    search_end_addr = end_addr;
    search_running = true;
    search_progress->setVisible(true);
    search_progress->setValue(0);
    search_progress_pct = 0;
    search_progress_timer->start();
    search_button->setEnabled(false);

    // FIXED: copy search params for thread safety - avoid race with UI / use-after-free
    const SearchType scan_type = current_search_type;
    const std::vector<u8> scan_value = search_value;
    const std::vector<bool> scan_aob_mask = search_aob_mask;
    const int scan_value_size = search_value_size;
    const u64 scan_start = start_addr;
    const u64 scan_end = end_addr;

    search_future = std::async(std::launch::async, [this, scan_type, scan_value, scan_aob_mask,
                                                    scan_value_size, scan_start, scan_end] {
        std::vector<u64> results;
        constexpr size_t chunk = 0x100000;  // 1MB per chunk
        const size_t total = scan_end - scan_start;
        size_t scanned = 0;
        QString error_msg;

        try {
            auto* proc = system.ApplicationProcess();
            if (!proc) {
                {
                    std::lock_guard lock(search_mutex);
                    search_error = tr("No game process.");
                }
                search_running = false;
                QMetaObject::invokeMethod(this, &MemoryViewerWidget::OnSearchComplete,
                                         Qt::QueuedConnection);
                return;
            }
            auto& mem = proc->GetMemory();

            if (scan_type == SearchType::Float && scan_value.size() >= 4) {
                float target_f;
                std::memcpy(&target_f, scan_value.data(), 4);
                LOG_DEBUG(Frontend,
                          "FIXED: Scanning for float {:.6f} ± {:.6f} in range 0x{:X}–0x{:X}",
                          target_f, FLOAT_SEARCH_EPSILON, scan_start, scan_end);
            }

            for (u64 addr = scan_start; addr < scan_end && results.size() < MAX_SEARCH_RESULTS;
                 addr += chunk) {
                // FIXED: safe memory read - validate range before read, skip invalid pages
                if (!mem.IsValidVirtualAddressRange(Common::ProcessAddress(addr), chunk)) {
                    scanned += chunk;
                    continue;
                }

                std::vector<u8> block(chunk);
                if (!mem.ReadBlock(Common::ProcessAddress(addr), block.data(), chunk)) {
                    scanned += chunk;
                    continue;
                }

                if (scan_type == SearchType::String) {
                    const size_t pattern_len = scan_value.size();
                    for (size_t i = 0; i + pattern_len <= block.size(); ++i) {
                        if (std::memcmp(block.data() + i, scan_value.data(), pattern_len) == 0) {
                            results.push_back(addr + i);
                        }
                    }
                } else if (scan_type == SearchType::AOB) {
                    const size_t pattern_len = scan_value.size();
                    for (size_t i = 0; i + pattern_len <= block.size(); ++i) {
                        bool match = true;
                        for (size_t k = 0; k < pattern_len; ++k) {
                            if (k >= scan_aob_mask.size() || !scan_aob_mask[k]) {
                                if (block[i + k] != scan_value[k]) {
                                    match = false;
                                    break;
                                }
                            }
                        }
                        if (match) results.push_back(addr + i);
                    }
                } else if (scan_type == SearchType::Float && scan_value.size() >= 4) {
                    // FIXED: float scan with epsilon tolerance - UltraCam 1.0→1.9 no crash
                    float target_f;
                    std::memcpy(&target_f, scan_value.data(), 4);
                    for (size_t i = 0; i + 4 <= block.size(); i += 4) {
                        u32 bits;
                        std::memcpy(&bits, block.data() + i, 4);
                        float mem_f;
                        std::memcpy(&mem_f, &bits, 4);
                        if (std::isfinite(mem_f) && std::isfinite(target_f) &&
                            std::fabs(mem_f - target_f) <= FLOAT_SEARCH_EPSILON) {
                            results.push_back(addr + i);
                        }
                    }
                } else {
                    const size_t step = static_cast<size_t>(scan_value_size);
                    for (size_t i = 0; i + step <= block.size(); i += step) {
                        if (std::memcmp(block.data() + i, scan_value.data(), step) == 0) {
                            results.push_back(addr + i);
                        }
                    }
                }

                scanned += chunk;
                const int pct = (total > 0) ? static_cast<int>((scanned * 100) / total) : 0;
                search_progress_pct.store(std::min(100, pct));
            }
        } catch (const std::exception& ex) {
            error_msg = tr("Search crashed: %1").arg(QString::fromUtf8(ex.what()));
            LOG_ERROR(Frontend, "FIXED: Memory scan exception - {}", ex.what());
        } catch (...) {
            error_msg = tr("Search crashed: unknown exception");
            LOG_ERROR(Frontend, "FIXED: Memory scan unknown exception");
        }

        {
            std::lock_guard lock(search_mutex);
            search_results = std::move(results);
            search_error = std::move(error_msg);
        }
        search_running = false;
        QMetaObject::invokeMethod(this, &MemoryViewerWidget::OnSearchComplete, Qt::QueuedConnection);
    });
}

void MemoryViewerWidget::OnSearchProgressTick() {
    if (!search_running) {
        search_progress_timer->stop();
        return;
    }
    search_progress->setValue(search_progress_pct.load());
}

void MemoryViewerWidget::OnSearchComplete() {
    search_progress_timer->stop();
    search_progress->setVisible(false);
    search_button->setEnabled(true);

    std::lock_guard lock(search_mutex);
    if (!search_error.isEmpty()) {
        status_label->setText(tr("Search error"));
        QMessageBox::critical(this, tr("Search Error"), search_error);
        search_error.clear();
        return;
    }
    if (search_results.empty()) {
        status_label->setText(tr("Search: no results"));
    } else {
        status_label->setText(tr("Search: %1 result(s). First: 0x%2")
                                  .arg(search_results.size())
                                  .arg(search_results[0], 16, 16, QLatin1Char('0')));
        GotoAddress(search_results[0]);
    }
}

void MemoryViewerWidget::OnMemoryRegionChanged(int index) {
    current_region_index = index;
    if (index >= 0 && index < static_cast<int>(regions.size())) {
        GotoAddress(regions[index].base);
    }
}

void MemoryViewerWidget::OnAddressInputReturnPressed() {
    auto addr = ParseAddress(address_input->text());
    if (addr) {
        GotoAddress(*addr);
    }
}

void MemoryViewerWidget::OnWatchListContextMenu(const QPoint& pos) {
    int row = watch_table->indexAt(pos).row();
    if (row < 0 || row >= static_cast<int>(watch_entries.size())) {
        return;
    }

    u64 addr = watch_entries[row].address;
    QMenu menu(this);
    menu.addAction(tr("Remove from watch list"), [this, row]() { RemoveFromWatchList(row); });
    menu.addAction(tr("Toggle freeze"), [this, row]() { ToggleWatchFreeze(row); });
    menu.addSeparator();
    menu.addAction(tr("Find what writes this address"), [this, addr]() {
        SetWatchpoint(addr, false, true);
    });
    menu.addAction(tr("Find what reads this address"), [this, addr]() {
        SetWatchpoint(addr, true, false);
    });
    menu.addAction(tr("Find what reads or writes"), [this, addr]() {
        SetWatchpoint(addr, true, true);
    });
    menu.exec(watch_table->mapToGlobal(pos));
}

void MemoryViewerWidget::OnHexViewContextMenu(const QPoint& pos) {
    QTextCursor cursor = hex_view->cursorForPosition(pos);
    int block = cursor.blockNumber();
    int col = cursor.columnNumber();
    if (block < 0 || col < 20) {
        return;
    }
    // Parse address from line "0x7100000000: ..."
    QString line = cursor.block().text();
    if (line.length() < 18) {
        return;
    }
    QString addr_str = line.left(18);
    bool ok;
    u64 base_addr = addr_str.mid(2).toULongLong(&ok, 16);
    if (!ok) {
        return;
    }
    int byte_col = (col - 20) / 3;
    if (byte_col < 0 || byte_col >= 16) {
        byte_col = 0;
    }
    u64 addr = base_addr + byte_col;

    QMenu menu(this);
    menu.addAction(tr("Add to watch list (1 byte)"), [this, addr]() { AddToWatchList(addr, 1); });
    menu.addAction(tr("Add to watch list (2 bytes)"), [this, addr]() { AddToWatchList(addr, 2); });
    menu.addAction(tr("Add to watch list (4 bytes)"), [this, addr]() { AddToWatchList(addr, 4); });
    menu.addAction(tr("Add to watch list (8 bytes)"), [this, addr]() { AddToWatchList(addr, 8); });
    menu.addAction(tr("Add to address list (1 byte)"), [this, addr]() {
        emit AddToAddressListRequested(addr, 1);
    });
    menu.addAction(tr("Add to address list (2 bytes)"), [this, addr]() {
        emit AddToAddressListRequested(addr, 2);
    });
    menu.addAction(tr("Add to address list (4 bytes)"), [this, addr]() {
        emit AddToAddressListRequested(addr, 4);
    });
    menu.addAction(tr("Add to address list (8 bytes)"), [this, addr]() {
        emit AddToAddressListRequested(addr, 8);
    });
    menu.addSeparator();
    menu.addAction(tr("Find what writes this address"), [this, addr]() {
        SetWatchpoint(addr, false, true);
    });
    menu.addAction(tr("Find what reads this address"), [this, addr]() {
        SetWatchpoint(addr, true, false);
    });
    menu.addAction(tr("Find what reads or writes"), [this, addr]() {
        SetWatchpoint(addr, true, true);
    });
    menu.addSeparator();
    menu.addAction(tr("Goto in disassembly"), [this, addr]() {
        emit AddressSelectedForDisassembly(addr);
    });
    menu.exec(hex_view->mapToGlobal(pos));
}

void MemoryViewerWidget::AddToWatchList(u64 address, int size) {
    std::lock_guard lock(watch_mutex);
    watch_entries.push_back({address, size, false, {}});
}

void MemoryViewerWidget::RemoveFromWatchList(int row) {
    std::lock_guard lock(watch_mutex);
    if (row >= 0 && row < static_cast<int>(watch_entries.size())) {
        watch_entries.erase(watch_entries.begin() + row);
    }
}

void MemoryViewerWidget::ToggleWatchFreeze(int row) {
    std::lock_guard lock(watch_mutex);
    if (row < 0 || row >= static_cast<int>(watch_entries.size())) {
        return;
    }
    auto& e = watch_entries[row];
    if (e.frozen) {
        e.frozen = false;
        e.frozen_value.clear();
    } else {
        auto* process = system.ApplicationProcess();
        if (!process) {
            return;
        }
        auto& mem = process->GetMemory();
        e.frozen_value.resize(e.size);
        if (mem.ReadBlock(Common::ProcessAddress(e.address), e.frozen_value.data(), e.size)) {
            e.frozen = true;
        }
    }
}

void MemoryViewerWidget::SetWatchpoint(u64 address, bool read, bool write) {
    auto* process = system.ApplicationProcess();
    if (!process) {
        QMessageBox::warning(this, tr("Watchpoint"),
                             tr("No game running. Watchpoints require an active process."));
        return;
    }

    using Kernel::DebugWatchpointType;
    DebugWatchpointType type = DebugWatchpointType::None;
    if (read && write) {
        type = DebugWatchpointType::ReadOrWrite;
    } else if (read) {
        type = DebugWatchpointType::Read;
    } else if (write) {
        type = DebugWatchpointType::Write;
    }

    bool ok = process->InsertWatchpoint(Kernel::KProcessAddress(address), 8, type);
    if (ok) {
        status_label->setText(tr("Watchpoint set at 0x%1 (%2)")
                                  .arg(address, 16, 16, QLatin1Char('0'))
                                  .arg(read && write ? tr("R/W") : read ? tr("R") : tr("W")));
    } else {
        QMessageBox::warning(this, tr("Watchpoint"),
                             tr("Failed to set watchpoint. Only %1 hardware watchpoints available.")
                                 .arg(NUM_WATCHPOINTS));
    }
}

void MemoryViewerWidget::RemoveWatchpoint(u64 address) {
    auto* process = system.ApplicationProcess();
    if (!process) {
        return;
    }
    process->RemoveWatchpoint(Kernel::KProcessAddress(address), 8,
                              Kernel::DebugWatchpointType::ReadOrWrite);
}

void MemoryViewerWidget::GotoAddress(u64 address) {
    address_input->setText(MemoryViewFormatAddress(address));
    RefreshMemoryView();
}

void MemoryViewerWidget::OnEmulationStarting() {
    regions.clear();
    current_process = nullptr;
    setEnabled(true);
}

void MemoryViewerWidget::OnEmulationStopping() {
    setEnabled(false);
    regions.clear();
    current_process = nullptr;
}

std::vector<u8> MemoryViewerWidget::ReadMemoryBlock(u64 address, size_t size) {
    std::vector<u8> buf(size);
    auto* process = system.ApplicationProcess();
    if (!process) {
        return {};
    }
    if (!process->GetMemory().ReadBlock(Common::ProcessAddress(address), buf.data(), size)) {
        return {};
    }
    return buf;
}

bool MemoryViewerWidget::WriteMemoryBlock(u64 address, const void* src_buffer, size_t size) {
    auto* process = system.ApplicationProcess();
    if (!process) {
        return false;
    }
    return process->GetMemory().WriteBlock(Common::ProcessAddress(address), src_buffer, size);
}

bool MemoryViewerWidget::IsAddressValid(u64 address) const {
    auto* process = system.ApplicationProcess();
    if (!process) {
        return false;
    }
    return process->GetMemory().IsValidVirtualAddress(Common::ProcessAddress(address));
}

std::optional<u64> MemoryViewerWidget::ParseAddress(const QString& text) const {
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("0x")) || t.startsWith(QStringLiteral("0X"))) {
        bool ok;
        qulonglong v = t.mid(2).toULongLong(&ok, 16);
        return ok ? std::optional<u64>(v) : std::nullopt;
    }
    bool ok;
    qulonglong v = t.toULongLong(&ok, 10);
    return ok ? std::optional<u64>(v) : std::nullopt;
}

