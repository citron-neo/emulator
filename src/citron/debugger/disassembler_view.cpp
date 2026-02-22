// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/debugger/disassembler_view.h"
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTextBlock>
#include <QVBoxLayout>

#include "common/typed_address.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"

namespace {

QString FormatAddress(u64 addr) {
    return QString::asprintf("0x%016llX", static_cast<unsigned long long>(addr));
}

} // namespace

DisassemblerViewWidget::DisassemblerViewWidget(Core::System& system_, QWidget* parent)
    : QWidget(parent), system(system_) {
    SetupUI();
}

DisassemblerViewWidget::~DisassemblerViewWidget() = default;

void DisassemblerViewWidget::SetupUI() {
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout();
    bar->addWidget(new QLabel(tr("Address:")));
    address_input = new QLineEdit(this);
    address_input->setPlaceholderText(tr("0x7100000000"));
    address_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(address_input, &QLineEdit::returnPressed, this, &DisassemblerViewWidget::OnGotoPressed);
    bar->addWidget(address_input);
    auto* goto_btn = new QPushButton(tr("Go to"), this);
    connect(goto_btn, &QPushButton::clicked, this, &DisassemblerViewWidget::OnGotoPressed);
    bar->addWidget(goto_btn);
    bar->addStretch();
    layout->addLayout(bar);

    disasm_view = new QPlainTextEdit(this);
    disasm_view->setReadOnly(true);
    disasm_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    disasm_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(disasm_view, &QPlainTextEdit::customContextMenuRequested, this,
            &DisassemblerViewWidget::OnContextMenu);
    layout->addWidget(disasm_view, 1);
}

void DisassemblerViewWidget::GotoAddress(u64 address) {
    current_address = address;
    address_input->setText(FormatAddress(address));
    RefreshView();
}

void DisassemblerViewWidget::RefreshView() {
    auto* proc = system.ApplicationProcess();
    if (!proc) {
        disasm_view->setPlainText(tr("No game running"));
        return;
    }
    auto& mem = proc->GetMemory();
    constexpr size_t lines = 64;
    constexpr size_t instr_size = 4;
    std::vector<u8> buf(lines * instr_size);
    if (!mem.ReadBlock(Common::ProcessAddress(current_address), buf.data(), buf.size())) {
        disasm_view->setPlainText(tr("(Cannot read memory at 0x%1)")
                                      .arg(current_address, 16, 16, QLatin1Char('0')));
        return;
    }
    QString out;
    for (size_t i = 0; i < lines; ++i) {
        u64 addr = current_address + i * instr_size;
        u32 insn;
        std::memcpy(&insn, buf.data() + i * 4, 4);
        out += FormatAddress(addr) + QStringLiteral("  ");
        out += QString::asprintf("%02X %02X %02X %02X  ", buf[i * 4], buf[i * 4 + 1],
                                 buf[i * 4 + 2], buf[i * 4 + 3]);
        out += QString::asprintf("  .long 0x%08X  ; (ARM64 - use Ghidra for full disasm)", insn);
        out += QLatin1Char('\n');
    }
    disasm_view->setPlainText(out);
}

void DisassemblerViewWidget::OnGotoPressed() {
    QString t = address_input->text().trimmed();
    bool hex = t.startsWith(QLatin1String("0x"));
    bool ok;
    qulonglong addr = t.mid(hex ? 2 : 0).toULongLong(&ok, hex ? 16 : 10);
    if (ok) {
        GotoAddress(static_cast<u64>(addr));
    }
}

void DisassemblerViewWidget::OnContextMenu(const QPoint& pos) {
    QTextCursor cursor = disasm_view->cursorForPosition(pos);
    int block = cursor.blockNumber();
    QString line = cursor.block().text();
    u64 addr = current_address + block * 4;
    if (line.length() < 18) return;
    QString addr_str = line.left(18).mid(2);
    bool ok;
    addr = addr_str.toULongLong(&ok, 16);
    if (!ok) return;

    QMenu menu(this);
    menu.addAction(tr("Go to address"), [this, addr]() {
        emit GotoAddressRequested(addr);
        GotoAddress(addr);
    });
    menu.addAction(tr("Go to in memory view"), [this, addr]() { emit GotoAddressRequested(addr); });
    menu.exec(disasm_view->mapToGlobal(pos));
}

void DisassemblerViewWidget::OnEmulationStarting() {
    setEnabled(true);
}

void DisassemblerViewWidget::OnEmulationStopping() {
    setEnabled(false);
}
