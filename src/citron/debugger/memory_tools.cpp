// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/debugger/memory_tools.h"
#include "citron/debugger/address_list.h"
#include "citron/debugger/disassembler_view.h"
#include "citron/debugger/function_browser.h"
#include "citron/debugger/memory_viewer.h"
#include <QVBoxLayout>

MemoryToolsWidget::MemoryToolsWidget(Core::System& system_, QWidget* parent)
    : QDockWidget(parent) {
    setObjectName(QStringLiteral("MemoryTools"));
    setWindowTitle(tr("Memory & Cheat Engine"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea |
                    Qt::BottomDockWidgetArea);

    tabs = new QTabWidget(this);
    memory_viewer = new MemoryViewerWidget(system_, this);
    address_list = new AddressListWidget(system_, this);
    function_browser = new FunctionBrowserWidget(system_, this);
    disassembler_view = new DisassemblerViewWidget(system_, this);

    tabs->addTab(memory_viewer, tr("Memory"));
    tabs->addTab(address_list, tr("Address List"));
    tabs->addTab(function_browser, tr("Functions"));
    tabs->addTab(disassembler_view, tr("Disassembly"));

    connect(function_browser, &FunctionBrowserWidget::AddressSelected, this,
            &MemoryToolsWidget::OnFunctionAddressSelected);
    connect(function_browser, &FunctionBrowserWidget::AddressSelected, memory_viewer,
            &MemoryViewerWidget::GotoAddress);
    connect(function_browser, &FunctionBrowserWidget::AddressSelected, disassembler_view,
            &DisassemblerViewWidget::GotoAddress);
    connect(address_list, &AddressListWidget::GotoAddressRequested, this,
            &MemoryToolsWidget::OnGotoAddress);
    connect(disassembler_view, &DisassemblerViewWidget::GotoAddressRequested, this,
            &MemoryToolsWidget::OnGotoAddress);
    connect(memory_viewer, &MemoryViewerWidget::AddressSelectedForDisassembly, disassembler_view,
            &DisassemblerViewWidget::GotoAddress);
    connect(memory_viewer, &MemoryViewerWidget::AddToAddressListRequested, address_list,
            &AddressListWidget::AddAddress);

    setWidget(tabs);
}

MemoryToolsWidget::~MemoryToolsWidget() = default;

QAction* MemoryToolsWidget::toggleViewAction() {
    return QDockWidget::toggleViewAction();
}

void MemoryToolsWidget::OnFunctionAddressSelected(u64 address) {
    OnGotoAddress(address);
}

void MemoryToolsWidget::OnGotoAddress(u64 address) {
    memory_viewer->GotoAddress(address);
    disassembler_view->GotoAddress(address);
    tabs->setCurrentWidget(memory_viewer);
}

void MemoryToolsWidget::OnEmulationStarting() {
    memory_viewer->OnEmulationStarting();
    function_browser->OnEmulationStarting();
    address_list->OnEmulationStarting();
    disassembler_view->OnEmulationStarting();
}

void MemoryToolsWidget::OnEmulationStopping() {
    memory_viewer->OnEmulationStopping();
    function_browser->OnEmulationStopping();
    address_list->OnEmulationStopping();
    disassembler_view->OnEmulationStopping();
}
