// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/debugger/function_browser.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/arm/debug.h"
#include "core/arm/symbols.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/loader/loader.h"

namespace {

QString FormatAddress(u64 addr) {
    return QString::asprintf("0x%016llX", static_cast<unsigned long long>(addr));
}

// Ghidra CSV export format: "Address","Name","Size" or simpler "Address","Name"
bool ParseGhidraCsvLine(const std::string& line, u64& out_addr, std::string& out_name) {
    if (line.empty()) {
        return false;
    }
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= line.size()) {
        return false;
    }
    std::string addr_str;
    if (line[i] == '"') {
        ++i;
        while (i < line.size() && line[i] != '"') {
            addr_str += line[i++];
        }
        if (i < line.size()) {
            ++i;  // skip closing quote
        }
    } else {
        while (i < line.size() && line[i] != ',' && line[i] != '\t') {
            addr_str += line[i++];
        }
    }
    while (i < line.size() && (line[i] == ',' || line[i] == '\t' || line[i] == ' ')) {
        ++i;
    }
    std::string name_str;
    if (i < line.size()) {
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                name_str += line[i++];
            }
        } else {
            while (i < line.size() && line[i] != '\r' && line[i] != '\n') {
                name_str += line[i++];
            }
        }
    }

    // Parse address - hex or decimal
    if (addr_str.empty()) {
        return false;
    }
    bool hex = (addr_str.size() >= 2 &&
                (addr_str.substr(0, 2) == "0x" || addr_str.substr(0, 2) == "0X"));
    try {
        out_addr = std::stoull(addr_str, nullptr, hex ? 16 : 10);
    } catch (...) {
        return false;
    }
    out_name = name_str;
    return true;
}

} // namespace

FunctionBrowserWidget::FunctionBrowserWidget(Core::System& system_, QWidget* parent)
    : QDockWidget(parent), system(system_) {
    setObjectName(QStringLiteral("FunctionBrowser"));
    setWindowTitle(tr("Memory & Functions - Function Browser"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea |
                    Qt::BottomDockWidgetArea);

    SetupUI();
}

FunctionBrowserWidget::~FunctionBrowserWidget() = default;

QAction* FunctionBrowserWidget::toggleViewAction() {
    return QDockWidget::toggleViewAction();
}

void FunctionBrowserWidget::SetupUI() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);

    // Filter
    auto* filter_bar = new QHBoxLayout();
    filter_bar->addWidget(new QLabel(tr("Filter:")));
    filter_input = new QLineEdit(this);
    filter_input->setPlaceholderText(tr("Filter by name or address..."));
    filter_input->setClearButtonEnabled(true);
    connect(filter_input, &QLineEdit::textChanged, this,
            &FunctionBrowserWidget::OnFilterTextChanged);
    filter_bar->addWidget(filter_input, 1);
    layout->addLayout(filter_bar);

    // Import/Export buttons
    auto* button_bar = new QHBoxLayout();
    auto* import_btn = new QPushButton(tr("Import from Ghidra CSV..."), this);
    connect(import_btn, &QPushButton::clicked, this, &FunctionBrowserWidget::OnImportGhidraCsv);
    button_bar->addWidget(import_btn);

    auto* export_btn = new QPushButton(tr("Export to Ghidra CSV..."), this);
    connect(export_btn, &QPushButton::clicked, this, &FunctionBrowserWidget::OnExportGhidraCsv);
    button_bar->addWidget(export_btn);

    button_bar->addStretch();
    layout->addLayout(button_bar);

    // Table
    table = new QTableWidget(this);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(
        {tr("Address"), tr("Name"), tr("Size"), tr("Module")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    connect(table, &QTableWidget::cellDoubleClicked, this,
            &FunctionBrowserWidget::OnTableDoubleClicked);
    layout->addWidget(table, 1);

    setWidget(container);
}

void FunctionBrowserWidget::RefreshFunctions() {
    LoadFromModules();
}

void FunctionBrowserWidget::LoadFromModules() {
    functions.clear();

    auto* process = system.ApplicationProcess();
    if (!process) {
        table->setRowCount(0);
        return;
    }

    auto modules = Core::FindModules(process);
    auto& memory = process->GetMemory();
    bool is_64 = process->Is64Bit();

    for (const auto& [base_addr, module_name] : modules) {
        auto symbols = Core::Symbols::GetSymbols(base_addr, memory, is_64);
        for (const auto& [sym_name, addr_size] : symbols) {
            u64 addr = base_addr + addr_size.first;  // base_addr + offset = absolute address
            u64 size = addr_size.second;

            std::string name = sym_name;
            auto it = ghidra_import_overrides.find(addr);
            if (it != ghidra_import_overrides.end()) {
                name = it->second;
            }

            functions.push_back({addr, name, size, module_name});
        }
    }

    // Add any Ghidra imports that weren't in symbols (e.g. manually named)
    for (const auto& [addr, name] : ghidra_import_overrides) {
        bool found = false;
        for (const auto& f : functions) {
            if (f.address == addr) {
                found = true;
                break;
            }
        }
        if (!found) {
            functions.push_back({addr, name, 0, "(imported)"});
        }
    }

    // Sort by address
    std::sort(functions.begin(), functions.end(),
              [](const FunctionEntry& a, const FunctionEntry& b) { return a.address < b.address; });

    OnFilterTextChanged(filter_input->text());
}

void FunctionBrowserWidget::OnFilterTextChanged(const QString& text) {
    QString q = text.trimmed().toLower();
    filtered_functions.clear();
    if (q.isEmpty()) {
        filtered_functions = functions;
    } else {
        std::string filter = q.toStdString();
        for (const auto& f : functions) {
            if (FormatAddress(f.address).toLower().contains(q) ||
                QString::fromStdString(f.name).toLower().contains(q) ||
                QString::fromStdString(f.module).toLower().contains(q)) {
                filtered_functions.push_back(f);
            }
        }
    }

    table->setSortingEnabled(false);
    table->setRowCount(static_cast<int>(filtered_functions.size()));
    for (int i = 0; i < static_cast<int>(filtered_functions.size()); ++i) {
        const auto& f = filtered_functions[i];
        table->setItem(i, 0, new QTableWidgetItem(FormatAddress(f.address)));
        table->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(f.name)));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(f.size)));
        table->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(f.module)));
    }
    table->setSortingEnabled(true);
}

void FunctionBrowserWidget::OnTableDoubleClicked(int row, int column) {
    (void)column;
    if (row >= 0 && row < static_cast<int>(filtered_functions.size())) {
        u64 addr = filtered_functions[row].address;
        emit AddressSelected(addr);
    }
}

void FunctionBrowserWidget::OnImportGhidraCsv() {
    QString path = QFileDialog::getOpenFileName(this, tr("Import Ghidra CSV"), QString(),
                                                tr("CSV Files (*.csv);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    std::ifstream f(path.toStdString());
    if (!f) {
        QMessageBox::critical(this, tr("Import"), tr("Could not open file: %1").arg(path));
        return;
    }

    ghidra_import_overrides.clear();
    std::string line;
    int count = 0;
    while (std::getline(f, line)) {
        u64 addr;
        std::string name;
        if (ParseGhidraCsvLine(line, addr, name) && !name.empty()) {
            ghidra_import_overrides[addr] = name;
            ++count;
        }
    }

    LoadFromModules();
    QMessageBox::information(this, tr("Import"),
                              tr("Imported %1 function name(s) from CSV.").arg(count));
}

void FunctionBrowserWidget::OnExportGhidraCsv() {
    QString path = QFileDialog::getSaveFileName(this, tr("Export Ghidra CSV"), QString(),
                                                tr("CSV Files (*.csv);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    std::ofstream f(path.toStdString());
    if (!f) {
        QMessageBox::critical(this, tr("Export"), tr("Could not create file: %1").arg(path));
        return;
    }

    for (const auto& fn : functions) {
        f << "\"0x" << std::hex << fn.address << std::dec << "\",\"" << fn.name << "\"," << fn.size
          << "\n";
    }

    QMessageBox::information(this, tr("Export"),
                              tr("Exported %1 function(s) to %2.").arg(functions.size()).arg(path));
}

void FunctionBrowserWidget::GotoAddress(u64 address) {
    // Parent handles jumping memory viewer - we just emit
    emit AddressSelected(address);
}

void FunctionBrowserWidget::OnEmulationStarting() {
    setEnabled(true);
    ghidra_import_overrides.clear();
    RefreshFunctions();
}

void FunctionBrowserWidget::OnEmulationStopping() {
    setEnabled(false);
}
