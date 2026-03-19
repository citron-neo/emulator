// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/debugger/address_list.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QInputDialog>
#include <QTimer>
#include <QMessageBox>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <fstream>

#include "common/fs/path_util.h"
#include "common/typed_address.h"
#include "common/swap.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"

namespace {

QString AddressListFormatAddress(u64 addr) {
    return QString::asprintf("0x%016llX", static_cast<unsigned long long>(addr));
}

} // namespace

AddressListWidget::AddressListWidget(Core::System& system_, QWidget* parent)
    : QWidget(parent), system(system_) {
    SetupUI();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AddressListWidget::UpdateTable);
    timer->start(100);
}

AddressListWidget::~AddressListWidget() = default;

void AddressListWidget::SetupUI() {
    auto* layout = new QVBoxLayout(this);

    auto* btn_bar = new QHBoxLayout();
    add_btn = new QPushButton(tr("Add Address"), this);
    connect(add_btn, &QPushButton::clicked, this, &AddressListWidget::OnAddAddress);
    btn_bar->addWidget(add_btn);

    remove_btn = new QPushButton(tr("Remove"), this);
    connect(remove_btn, &QPushButton::clicked, this, &AddressListWidget::OnRemoveAddress);
    btn_bar->addWidget(remove_btn);

    save_btn = new QPushButton(tr("Save Table..."), this);
    connect(save_btn, &QPushButton::clicked, this, &AddressListWidget::OnSaveTable);
    btn_bar->addWidget(save_btn);

    load_btn = new QPushButton(tr("Load Table..."), this);
    connect(load_btn, &QPushButton::clicked, this, &AddressListWidget::OnLoadTable);
    btn_bar->addWidget(load_btn);

    pointer_scan_btn = new QPushButton(tr("Pointer Scan"), this);
    connect(pointer_scan_btn, &QPushButton::clicked, this, &AddressListWidget::OnPointerScan);
    btn_bar->addWidget(pointer_scan_btn);

    btn_bar->addStretch();
    layout->addLayout(btn_bar);

    table = new QTableWidget(this);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(
        {tr("Address"), tr("Type"), tr("Value"), tr("Frozen"), tr("Description")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int col) {
                if (col == 2) {
                    OnEditValue(row, col);
                }
            });
    connect(table, &QTableWidget::customContextMenuRequested, this,
            &AddressListWidget::OnContextMenu);
    layout->addWidget(table, 1);
}

void AddressListWidget::AddAddress(u64 address, int type_size) {
    ValueType vt = ValueType::U32;
    if (type_size == 1) vt = ValueType::U8;
    else if (type_size == 2) vt = ValueType::U16;
    else if (type_size == 4) vt = ValueType::U32;
    else if (type_size == 8) vt = ValueType::U64;
    entries.push_back({address, vt, false, {}, QString()});
    UpdateTable();
}

void AddressListWidget::UpdateTable() {
    table->setRowCount(static_cast<int>(entries.size()));
    auto* proc = system.ApplicationProcess();
    auto* mem = proc ? &proc->GetMemory() : nullptr;

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(AddressListFormatAddress(e.address)));

        QString type_str = QStringLiteral("u32");
        int size = 4;
        switch (e.type) {
        case ValueType::U8: type_str = QStringLiteral("u8"); size = 1; break;
        case ValueType::U16: type_str = QStringLiteral("u16"); size = 2; break;
        case ValueType::U32: type_str = QStringLiteral("u32"); size = 4; break;
        case ValueType::U64: type_str = QStringLiteral("u64"); size = 8; break;
        case ValueType::Float: type_str = QStringLiteral("float"); size = 4; break;
        case ValueType::Double: type_str = QStringLiteral("double"); size = 8; break;
        }
        table->setItem(static_cast<int>(i), 1, new QTableWidgetItem(type_str));

        QString val_str;
        if (e.frozen && !e.frozen_value.empty()) {
            if (e.type == ValueType::Float && e.frozen_value.size() >= 4) {
                u32 bits;
                std::memcpy(&bits, e.frozen_value.data(), 4);
                float f;
                std::memcpy(&f, &bits, 4);
                val_str = QString::number(f);
            } else if (e.type == ValueType::Double && e.frozen_value.size() >= 8) {
                u64 bits;
                std::memcpy(&bits, e.frozen_value.data(), 8);
                double d;
                std::memcpy(&d, &bits, 8);
                val_str = QString::number(d);
            } else if (size == 1) {
                val_str = QString::asprintf("0x%02X", e.frozen_value[0]);
            } else if (size == 2) {
                u16 v;
                std::memcpy(&v, e.frozen_value.data(), 2);
                val_str = QString::asprintf("0x%04X", Common::swap16(v));
            } else if (size == 4) {
                u32 v;
                std::memcpy(&v, e.frozen_value.data(), 4);
                val_str = QString::asprintf("0x%08X", Common::swap32(v));
            } else {
                u64 v;
                std::memcpy(&v, e.frozen_value.data(), 8);
                val_str = QString::asprintf("0x%016llX",
                    static_cast<unsigned long long>(Common::swap64(v)));
            }
        } else if (mem && mem->IsValidVirtualAddress(Common::ProcessAddress(e.address))) {
            try {
                if (size == 1) val_str = QString::asprintf("0x%02X", mem->Read8(e.address));
                else if (size == 2) val_str = QString::asprintf("0x%04X", mem->Read16(e.address));
                else if (size == 4 && e.type != ValueType::Float) {
                    val_str = QString::asprintf("0x%08X", mem->Read32(e.address));
                } else if (size == 4 && e.type == ValueType::Float) {
                    u32 v = mem->Read32(e.address);
                    float f;
                    std::memcpy(&f, &v, 4);
                    val_str = QString::number(f);
                } else if (size == 8 && e.type != ValueType::Double) {
                    val_str = QString::asprintf("0x%016llX",
                        static_cast<unsigned long long>(mem->Read64(e.address)));
                } else if (size == 8 && e.type == ValueType::Double) {
                    u64 v = mem->Read64(e.address);
                    double d;
                    std::memcpy(&d, &v, 8);
                    val_str = QString::number(d);
                }
            } catch (...) {
                val_str = tr("(error)");
            }
        } else {
            val_str = tr("(invalid)");
        }
        table->setItem(static_cast<int>(i), 2, new QTableWidgetItem(val_str));
        auto* freeze_item = new QTableWidgetItem(e.frozen ? tr("Yes") : tr("No"));
        freeze_item->setFlags(freeze_item->flags() | Qt::ItemIsUserCheckable);
        freeze_item->setCheckState(e.frozen ? Qt::Checked : Qt::Unchecked);
        table->setItem(static_cast<int>(i), 3, freeze_item);
        table->setItem(static_cast<int>(i), 4, new QTableWidgetItem(e.description));
    }
    ApplyFrozenValues();
}

void AddressListWidget::OnAddAddress() {
    bool ok;
    QString text = QInputDialog::getText(this, tr("Add Address"), tr("Address (hex):"),
                                         QLineEdit::Normal, QStringLiteral("0x7100000000"), &ok);
    if (!ok || text.isEmpty()) return;
    text = text.trimmed();
    bool hex = text.startsWith(QLatin1String("0x"));
    qulonglong addr = text.mid(hex ? 2 : 0).toULongLong(&ok, hex ? 16 : 10);
    if (!ok) {
        QMessageBox::warning(this, tr("Add Address"), tr("Invalid address."));
        return;
    }
    AddAddress(static_cast<u64>(addr), 4);
}

void AddressListWidget::OnRemoveAddress() {
    int row = table->currentRow();
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    entries.erase(entries.begin() + row);
    UpdateTable();
}

void AddressListWidget::OnToggleFreeze(int row) {
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    auto& e = entries[row];
    auto* proc = system.ApplicationProcess();
    if (!proc) return;
    auto& mem = proc->GetMemory();
    int size = 4;
    switch (e.type) {
    case ValueType::U8: size = 1; break;
    case ValueType::U16: size = 2; break;
    case ValueType::U32:
    case ValueType::Float: size = 4; break;
    case ValueType::U64:
    case ValueType::Double: size = 8; break;
    }
    if (e.frozen) {
        e.frozen = false;
        e.frozen_value.clear();
    } else if (mem.IsValidVirtualAddress(Common::ProcessAddress(e.address))) {
        e.frozen_value.resize(size);
        if (mem.ReadBlock(Common::ProcessAddress(e.address), e.frozen_value.data(), size)) {
            e.frozen = true;
        }
    }
    UpdateTable();
}

void AddressListWidget::OnEditValue(int row, int column) {
    (void)column;
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    auto& e = entries[row];
    auto* proc = system.ApplicationProcess();
    if (!proc) return;
    auto& mem = proc->GetMemory();
    int size = 4;
    if (e.type == ValueType::U8) size = 1;
    else if (e.type == ValueType::U16) size = 2;
    else if (e.type == ValueType::U64 || e.type == ValueType::Double) size = 8;

    QString cur = table->item(row, 2) ? table->item(row, 2)->text() : QString();
    bool ok;
    QString text = QInputDialog::getText(this, tr("Edit Value"), tr("New value:"),
                                         QLineEdit::Normal, cur, &ok);
    if (!ok || text.isEmpty()) return;
    text = text.trimmed();

    std::vector<u8> buf(size);
    if (e.type == ValueType::Float) {
        float f = text.toFloat(&ok);
        if (!ok) return;
        std::memcpy(buf.data(), &f, 4);
    } else if (e.type == ValueType::Double) {
        double d = text.toDouble(&ok);
        if (!ok) return;
        std::memcpy(buf.data(), &d, 8);
    } else {
        bool hex = text.startsWith(QLatin1String("0x"));
        qulonglong v = text.mid(hex ? 2 : 0).toULongLong(&ok, hex ? 16 : 10);
        if (!ok) return;
        if (size == 1) buf[0] = static_cast<u8>(v);
        else if (size == 2) {
            u16 uv = Common::swap16(static_cast<u16>(v));
            std::memcpy(buf.data(), &uv, 2);
        } else if (size == 4) {
            u32 uv = Common::swap32(static_cast<u32>(v));
            std::memcpy(buf.data(), &uv, 4);
        } else {
            u64 uv = Common::swap64(static_cast<u64>(v));
            std::memcpy(buf.data(), &uv, 8);
        }
    }
    mem.WriteBlock(Common::ProcessAddress(e.address), buf.data(), size);
    if (e.frozen) {
        e.frozen_value = buf;
    }
    UpdateTable();
}

void AddressListWidget::OnContextMenu(const QPoint& pos) {
    int row = table->indexAt(pos).row();
    QMenu menu(this);
    if (row >= 0 && row < static_cast<int>(entries.size())) {
        menu.addAction(tr("Remove"), [this, row]() {
            if (row < static_cast<int>(entries.size())) {
                entries.erase(entries.begin() + row);
                UpdateTable();
            }
        });
        menu.addAction(tr("Toggle freeze"), [this, row]() { OnToggleFreeze(row); });
        menu.addAction(tr("Edit value"), [this, row]() { OnEditValue(row, 2); });
        menu.addAction(tr("Go to address"), [this, row]() {
            emit GotoAddressRequested(entries[row].address);
        });
        menu.addAction(tr("Pointer scan for this address"), [this, row]() {
            table->setCurrentCell(row, 0);
            OnPointerScan();
        });
    }
    menu.exec(table->mapToGlobal(pos));
}

void AddressListWidget::OnPointerScan() {
    int row = table->currentRow();
    if (row < 0 || row >= static_cast<int>(entries.size())) {
        QMessageBox::information(this, tr("Pointer Scan"),
                                 tr("Select an address in the table first."));
        return;
    }
    u64 target = entries[row].address;
    auto* proc = system.ApplicationProcess();
    if (!proc) {
        QMessageBox::warning(this, tr("Pointer Scan"), tr("No game running."));
        return;
    }
    auto& mem = proc->GetMemory();
    const auto& pt = proc->GetPageTable();
    u64 heap_base = GetInteger(pt.GetHeapRegionStart());
    u64 heap_size = pt.GetHeapRegionSize();
    std::vector<u64> pointers;
    const size_t chunk = 0x10000;
    for (u64 addr = heap_base; addr < heap_base + heap_size && pointers.size() < 1000;
         addr += chunk) {
        std::vector<u8> block(chunk);
        if (!mem.ReadBlock(Common::ProcessAddress(addr), block.data(), chunk)) continue;
        for (size_t i = 0; i + 8 <= block.size(); i += 8) {
            u64 val;
            std::memcpy(&val, block.data() + i, 8);
            if (val == target) {
                pointers.push_back(addr + i);
            }
        }
    }
    for (u64 p : pointers) {
        AddAddress(p, 8);
    }
    QMessageBox::information(this, tr("Pointer Scan"),
                             tr("Found %1 pointer(s) to 0x%2.")
                                 .arg(pointers.size())
                                 .arg(target, 16, 16, QLatin1Char('0')));
}

void AddressListWidget::OnSaveTable() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save Cheat Table"), QString(),
                                                tr("Cheat Table (*.ct.json);;All (*)"));
    if (path.isEmpty()) return;
    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : entries) {
        nlohmann::json ent;
        ent["address"] = std::to_string(e.address);
        ent["type"] = static_cast<int>(e.type);
        ent["frozen"] = e.frozen;
        ent["description"] = e.description.toStdString();
        if (e.frozen && !e.frozen_value.empty()) {
            std::string hex;
            for (u8 b : e.frozen_value) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X", b);
                hex += buf;
            }
            ent["frozen_value_hex"] = hex;
        }
        j.push_back(ent);
    }
    std::ofstream f(path.toStdString());
    if (f) f << j.dump(2);
}

void AddressListWidget::OnLoadTable() {
    QString path = QFileDialog::getOpenFileName(this, tr("Load Cheat Table"), QString(),
                                                tr("Cheat Table (*.ct.json);;All (*)"));
    if (path.isEmpty()) return;
    std::ifstream f(path.toStdString());
    if (!f) {
        QMessageBox::critical(this, tr("Load"), tr("Could not open file."));
        return;
    }
    try {
        auto j = nlohmann::json::parse(f);
        entries.clear();
        for (const auto& ent : j) {
            CheatEntry e{};
            e.address = std::stoull(ent["address"].get<std::string>(), nullptr, 0);
            e.type = static_cast<ValueType>(ent.value("type", 2));  // 2 = U32
            e.frozen = ent.value("frozen", false);
            e.description = QString::fromStdString(ent.value("description", ""));
            if (e.frozen && ent.contains("frozen_value_hex")) {
                std::string hex = ent["frozen_value_hex"];
                for (size_t i = 0; i + 1 < hex.size(); i += 2) {
                    e.frozen_value.push_back(
                        static_cast<u8>(std::stoul(hex.substr(i, 2), nullptr, 16)));
                }
            }
            entries.push_back(e);
        }
        UpdateTable();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, tr("Load"),
                              tr("Parse error: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void AddressListWidget::ApplyFrozenValues() {
    auto* proc = system.ApplicationProcess();
    if (!proc) return;
    auto& mem = proc->GetMemory();
    for (const auto& e : entries) {
        if (e.frozen && !e.frozen_value.empty() &&
            mem.IsValidVirtualAddress(Common::ProcessAddress(e.address))) {
            mem.WriteBlock(Common::ProcessAddress(e.address), e.frozen_value.data(),
                           e.frozen_value.size());
        }
    }
}

void AddressListWidget::OnEmulationStarting() {
    setEnabled(true);
}

void AddressListWidget::OnEmulationStopping() {
    setEnabled(false);
}
