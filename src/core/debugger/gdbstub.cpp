// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <codecvt>
#include <locale>
#include <numeric>
#include <optional>
#include <thread>

#include <boost/algorithm/string.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "common/hex_util.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/arm/arm_interface.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "core/debugger/gdbstub.h"
#include "core/debugger/gdbstub_arch.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/loader/loader.h"
#include "core/memory.h"

namespace Core {

constexpr char GDB_STUB_START = '$';
constexpr char GDB_STUB_END = '#';
constexpr char GDB_STUB_ACK = '+';
constexpr char GDB_STUB_NACK = '-';
constexpr char GDB_STUB_INT3 = 0x03;
constexpr int GDB_STUB_SIGTRAP = 5;

constexpr char GDB_STUB_REPLY_ERR[] = "E01";
constexpr char GDB_STUB_REPLY_OK[] = "OK";
constexpr char GDB_STUB_REPLY_EMPTY[] = "";

static u8 CalculateChecksum(std::string_view data) {
    return std::accumulate(data.begin(), data.end(), u8{0},
                           [](u8 lhs, u8 rhs) { return static_cast<u8>(lhs + rhs); });
}

static std::string EscapeGDB(std::string_view data) {
    std::string escaped;
    escaped.reserve(data.size());

    for (char c : data) {
        switch (c) {
        case '#':
            escaped += "}\x03";
            break;
        case '$':
            escaped += "}\x04";
            break;
        case '*':
            escaped += "}\x0a";
            break;
        case '}':
            escaped += "}\x5d";
            break;
        default:
            escaped += c;
            break;
        }
    }

    return escaped;
}

static std::string EscapeXML(std::string_view data) {
    std::u32string converted = U"[Encoding error]";
    try {
        converted = Common::UTF8ToUTF32(data);
    } catch (std::range_error&) {
    }

    std::string escaped;
    escaped.reserve(data.size());

    for (char32_t c : converted) {
        switch (c) {
        case '&':
            escaped += "&amp;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            if (c > 0x7f) {
                escaped += fmt::format("&#{};", static_cast<u32>(c));
            } else {
                escaped += static_cast<char>(c);
            }
            break;
        }
    }

    return escaped;
}

GDBStub::GDBStub(DebuggerBackend& backend_, Core::System& system_, Kernel::KProcess* debug_process_)
    : DebuggerFrontend(backend_), system{system_}, debug_process{debug_process_} {
    if (GetProcess()->Is64Bit()) {
        arch = std::make_unique<GDBStubA64>();
    } else {
        arch = std::make_unique<GDBStubA32>();
    }
}

GDBStub::~GDBStub() = default;

void GDBStub::Connected() {}

void GDBStub::ShuttingDown() {}

void GDBStub::Stopped(Kernel::KThread* thread) {
    SendReply(arch->ThreadStatus(thread, GDB_STUB_SIGTRAP));
}

void GDBStub::Watchpoint(Kernel::KThread* thread, const Kernel::DebugWatchpoint& watch) {
    const auto status{arch->ThreadStatus(thread, GDB_STUB_SIGTRAP)};

    switch (watch.type) {
    case Kernel::DebugWatchpointType::Read:
        SendReply(fmt::format("{}rwatch:{:x};", status, GetInteger(watch.start_address)));
        break;
    case Kernel::DebugWatchpointType::Write:
        SendReply(fmt::format("{}watch:{:x};", status, GetInteger(watch.start_address)));
        break;
    case Kernel::DebugWatchpointType::ReadOrWrite:
    default:
        SendReply(fmt::format("{}awatch:{:x};", status, GetInteger(watch.start_address)));
        break;
    }
}

std::vector<DebuggerAction> GDBStub::ClientData(std::span<const u8> data) {
    std::vector<DebuggerAction> actions;
    current_command.insert(current_command.end(), data.begin(), data.end());

    while (current_command.size() != 0) {
        ProcessData(actions);
    }

    return actions;
}

void GDBStub::ProcessData(std::vector<DebuggerAction>& actions) {
    const char c{current_command[0]};

    // Acknowledgement
    if (c == GDB_STUB_ACK || c == GDB_STUB_NACK) {
        current_command.erase(current_command.begin());
        return;
    }

    // Interrupt
    if (c == GDB_STUB_INT3) {
        LOG_INFO(Debug_GDBStub, "Received interrupt");
        current_command.erase(current_command.begin());
        actions.push_back(DebuggerAction::Interrupt);
        SendStatus(GDB_STUB_ACK);
        return;
    }

    // Otherwise, require the data to be the start of a command
    if (c != GDB_STUB_START) {
        LOG_ERROR(Debug_GDBStub, "Invalid command buffer contents: {}", current_command.data());
        current_command.clear();
        SendStatus(GDB_STUB_NACK);
        return;
    }

    // Continue reading until command is complete
    while (CommandEnd() == current_command.end()) {
        const auto new_data{backend.ReadFromClient()};
        current_command.insert(current_command.end(), new_data.begin(), new_data.end());
    }

    // Execute and respond to GDB
    const auto command{DetachCommand()};

    if (command) {
        SendStatus(GDB_STUB_ACK);
        ExecuteCommand(*command, actions);
    } else {
        SendStatus(GDB_STUB_NACK);
    }
}

void GDBStub::ExecuteCommand(std::string_view packet, std::vector<DebuggerAction>& actions) {
    LOG_TRACE(Debug_GDBStub, "Executing command: {}", packet);

    if (packet.length() == 0) {
        SendReply(GDB_STUB_REPLY_ERR);
        return;
    }

    if (packet.starts_with("vCont")) {
        HandleVCont(packet.substr(5), actions);
        return;
    }

    std::string_view command{packet.substr(1, packet.size())};

    switch (packet[0]) {
    case 'H': {
        Kernel::KThread* thread{nullptr};
        s64 thread_id{strtoll(command.data() + 1, nullptr, 16)};
        if (thread_id >= 1) {
            thread = GetThreadByID(thread_id);
        } else {
            thread = backend.GetActiveThread();
        }

        if (thread) {
            SendReply(GDB_STUB_REPLY_OK);
            backend.SetActiveThread(thread);
        } else {
            SendReply(GDB_STUB_REPLY_ERR);
        }
        break;
    }
    case 'T': {
        s64 thread_id{strtoll(command.data(), nullptr, 16)};
        if (GetThreadByID(thread_id)) {
            SendReply(GDB_STUB_REPLY_OK);
        } else {
            SendReply(GDB_STUB_REPLY_ERR);
        }
        break;
    }
    case 'Q':
    case 'q':
        HandleQuery(command);
        break;
    case '?':
        SendReply(arch->ThreadStatus(backend.GetActiveThread(), GDB_STUB_SIGTRAP));
        break;
    case 'k':
        LOG_INFO(Debug_GDBStub, "Shutting down emulation");
        actions.push_back(DebuggerAction::ShutdownEmulation);
        break;
    case 'g':
        SendReply(arch->ReadRegisters(backend.GetActiveThread()));
        break;
    case 'G':
        arch->WriteRegisters(backend.GetActiveThread(), command);
        SendReply(GDB_STUB_REPLY_OK);
        break;
    case 'p': {
        const size_t reg{static_cast<size_t>(strtoll(command.data(), nullptr, 16))};
        SendReply(arch->RegRead(backend.GetActiveThread(), reg));
        break;
    }
    case 'P': {
        const auto sep{std::find(command.begin(), command.end(), '=') - command.begin() + 1};
        const size_t reg{static_cast<size_t>(strtoll(command.data(), nullptr, 16))};
        arch->RegWrite(backend.GetActiveThread(), reg, std::string_view(command).substr(sep));
        SendReply(GDB_STUB_REPLY_OK);
        break;
    }
    case 'm': {
        const auto sep{std::find(command.begin(), command.end(), ',') - command.begin() + 1};
        const size_t addr{static_cast<size_t>(strtoll(command.data(), nullptr, 16))};
        const size_t size{static_cast<size_t>(strtoll(command.data() + sep, nullptr, 16))};

        std::vector<u8> mem(size);
        if (GetMemory().ReadBlock(addr, mem.data(), size)) {
            // Restore any bytes belonging to replaced instructions.
            auto it = replaced_instructions.lower_bound(addr);
            for (; it != replaced_instructions.end() && it->first < addr + size; it++) {
                // Get the bytes of the instruction we previously replaced.
                const u32 original_bytes = it->second;

                // Calculate where to start writing to the output buffer.
                const size_t output_offset = it->first - addr;

                // Calculate how many bytes to write.
                // The loop condition ensures output_offset < size.
                const size_t n = std::min<size_t>(size - output_offset, sizeof(u32));

                // Write the bytes to the output buffer.
                std::memcpy(mem.data() + output_offset, &original_bytes, n);
            }

            SendReply(Common::HexToString(mem));
        } else {
            SendReply(GDB_STUB_REPLY_ERR);
        }
        break;
    }
    case 'M': {
        const auto size_sep{std::find(command.begin(), command.end(), ',') - command.begin() + 1};
        const auto mem_sep{std::find(command.begin(), command.end(), ':') - command.begin() + 1};

        const size_t addr{static_cast<size_t>(strtoll(command.data(), nullptr, 16))};
        const size_t size{static_cast<size_t>(strtoll(command.data() + size_sep, nullptr, 16))};

        const auto mem_substr{std::string_view(command).substr(mem_sep)};
        const auto mem{Common::HexStringToVector(mem_substr, false)};

        if (GetMemory().WriteBlock(addr, mem.data(), size)) {
            Core::InvalidateInstructionCacheRange(GetProcess(), addr, size);
            SendReply(GDB_STUB_REPLY_OK);
        } else {
            SendReply(GDB_STUB_REPLY_ERR);
        }
        break;
    }
    case 's':
        actions.push_back(DebuggerAction::StepThreadLocked);
        break;
    case 'C':
    case 'c':
        actions.push_back(DebuggerAction::Continue);
        break;
    case 'Z':
        HandleBreakpointInsert(command);
        break;
    case 'z':
        HandleBreakpointRemove(command);
        break;
    default:
        SendReply(GDB_STUB_REPLY_EMPTY);
        break;
    }
}

enum class BreakpointType {
    Software = 0,
    Hardware = 1,
    WriteWatch = 2,
    ReadWatch = 3,
    AccessWatch = 4,
};

void GDBStub::HandleBreakpointInsert(std::string_view command) {
    const auto type{static_cast<BreakpointType>(strtoll(command.data(), nullptr, 16))};
    const auto addr_sep{std::find(command.begin(), command.end(), ',') - command.begin() + 1};
    const auto size_sep{std::find(command.begin() + addr_sep, command.end(), ',') -
                        command.begin() + 1};
    const size_t addr{static_cast<size_t>(strtoll(command.data() + addr_sep, nullptr, 16))};
    const size_t size{static_cast<size_t>(strtoll(command.data() + size_sep, nullptr, 16))};

    if (!GetMemory().IsValidVirtualAddressRange(addr, size)) {
        SendReply(GDB_STUB_REPLY_ERR);
        return;
    }

    bool success{};

    switch (type) {
    case BreakpointType::Software:
        replaced_instructions[addr] = GetMemory().Read32(addr);
        GetMemory().Write32(addr, arch->BreakpointInstruction());
        Core::InvalidateInstructionCacheRange(GetProcess(), addr, sizeof(u32));
        success = true;
        break;
    case BreakpointType::WriteWatch:
        success = GetProcess()->InsertWatchpoint(addr, size, Kernel::DebugWatchpointType::Write);
        break;
    case BreakpointType::ReadWatch:
        success = GetProcess()->InsertWatchpoint(addr, size, Kernel::DebugWatchpointType::Read);
        break;
    case BreakpointType::AccessWatch:
        success =
            GetProcess()->InsertWatchpoint(addr, size, Kernel::DebugWatchpointType::ReadOrWrite);
        break;
    case BreakpointType::Hardware:
    default:
        SendReply(GDB_STUB_REPLY_EMPTY);
        return;
    }

    if (success) {
        SendReply(GDB_STUB_REPLY_OK);
    } else {
        SendReply(GDB_STUB_REPLY_ERR);
    }
}

void GDBStub::HandleBreakpointRemove(std::string_view command) {
    const auto type{static_cast<BreakpointType>(strtoll(command.data(), nullptr, 16))};
    const auto addr_sep{std::find(command.begin(), command.end(), ',') - command.begin() + 1};
    const auto size_sep{std::find(command.begin() + addr_sep, command.end(), ',') -
                        command.begin() + 1};
    const size_t addr{static_cast<size_t>(strtoll(command.data() + addr_sep, nullptr, 16))};
    const size_t size{static_cast<size_t>(strtoll(command.data() + size_sep, nullptr, 16))};

    if (!GetMemory().IsValidVirtualAddressRange(addr, size)) {
        SendReply(GDB_STUB_REPLY_ERR);
        return;
    }

    bool success{};

    switch (type) {
    case BreakpointType::Software: {
        const auto orig_insn{replaced_instructions.find(addr)};
        if (orig_insn != replaced_instructions.end()) {
            GetMemory().Write32(addr, orig_insn->second);
            Core::InvalidateInstructionCacheRange(GetProcess(), addr, sizeof(u32));
            replaced_instructions.erase(addr);
            success = true;
        }
        break;
    }
    case BreakpointType::WriteWatch:
        success = GetProcess()->RemoveWatchpoint(addr, size, Kernel::DebugWatchpointType::Write);
        break;
    case BreakpointType::ReadWatch:
        success = GetProcess()->RemoveWatchpoint(addr, size, Kernel::DebugWatchpointType::Read);
        break;
    case BreakpointType::AccessWatch:
        success =
            GetProcess()->RemoveWatchpoint(addr, size, Kernel::DebugWatchpointType::ReadOrWrite);
        break;
    case BreakpointType::Hardware:
    default:
        SendReply(GDB_STUB_REPLY_EMPTY);
        return;
    }

    if (success) {
        SendReply(GDB_STUB_REPLY_OK);
    } else {
        SendReply(GDB_STUB_REPLY_ERR);
    }
}

static std::string PaginateBuffer(std::string_view buffer, std::string_view request) {
    const auto amount{request.substr(request.find(',') + 1)};
    const auto offset_val{static_cast<u64>(strtoll(request.data(), nullptr, 16))};
    const auto amount_val{static_cast<u64>(strtoll(amount.data(), nullptr, 16))};

    if (offset_val + amount_val > buffer.size()) {
        return fmt::format("l{}", buffer.substr(offset_val));
    } else {
        return fmt::format("m{}", buffer.substr(offset_val, amount_val));
    }
}

void GDBStub::HandleQuery(std::string_view command) {
    if (command.starts_with("TStatus")) {
        // no tracepoint support
        SendReply("T0");
    } else if (command.starts_with("Supported")) {
        SendReply("PacketSize=4000;qXfer:features:read+;qXfer:threads:read+;qXfer:libraries:read+;"
                  "vContSupported+;QStartNoAckMode+");
    } else if (command.starts_with("Xfer:features:read:target.xml:")) {
        const auto target_xml{arch->GetTargetXML()};
        SendReply(PaginateBuffer(target_xml, command.substr(30)));
    } else if (command.starts_with("Offsets")) {
        const auto main_offset = Core::FindMainModuleEntrypoint(GetProcess());
        SendReply(fmt::format("TextSeg={:x}", GetInteger(main_offset)));
    } else if (command.starts_with("Xfer:libraries:read::")) {
        auto modules = Core::FindModules(GetProcess());

        std::string buffer;
        buffer += R"(<?xml version="1.0"?>)";
        buffer += "<library-list>";
        for (const auto& [base, name] : modules) {
            buffer += fmt::format(R"(<library name="{}"><segment address="{:#x}"/></library>)",
                                  EscapeXML(name), base);
        }
        buffer += "</library-list>";

        SendReply(PaginateBuffer(buffer, command.substr(21)));
    } else if (command.starts_with("fThreadInfo")) {
        // beginning of list
        const auto& threads = GetProcess()->GetThreadList();
        std::vector<std::string> thread_ids;
        for (const auto& thread : threads) {
            thread_ids.push_back(fmt::format("{:x}", thread.GetThreadId()));
        }
        SendReply(fmt::format("m{}", fmt::join(thread_ids, ",")));
    } else if (command.starts_with("sThreadInfo")) {
        // end of list
        SendReply("l");
    } else if (command.starts_with("Xfer:threads:read::")) {
        std::string buffer;
        buffer += R"(<?xml version="1.0"?>)";
        buffer += "<threads>";

        const auto& threads = GetProcess()->GetThreadList();
        for (const auto& thread : threads) {
            auto thread_name{Core::GetThreadName(&thread)};
            if (!thread_name) {
                thread_name = fmt::format("Thread {:d}", thread.GetThreadId());
            }

            buffer += fmt::format(R"(<thread id="{:x}" core="{:d}" name="{}">{}</thread>)",
                                  thread.GetThreadId(), thread.GetActiveCore(),
                                  EscapeXML(*thread_name), GetThreadState(&thread));
        }

        buffer += "</threads>";

        SendReply(PaginateBuffer(buffer, command.substr(19)));
    } else if (command.starts_with("Attached")) {
        SendReply("0");
    } else if (command.starts_with("StartNoAckMode")) {
        no_ack = true;
        SendReply(GDB_STUB_REPLY_OK);
    } else if (command.starts_with("Rcmd,")) {
        HandleRcmd(Common::HexStringToVector(command.substr(5), false));
    } else {
        SendReply(GDB_STUB_REPLY_EMPTY);
    }
}

void GDBStub::HandleVCont(std::string_view command, std::vector<DebuggerAction>& actions) {
    if (command == "?") {
        // Continuing and stepping are supported
        // (signal is ignored, but required for GDB to use vCont)
        SendReply("vCont;c;C;s;S");
        return;
    }

    Kernel::KThread* stepped_thread{nullptr};
    bool lock_execution{true};

    std::vector<std::string> entries;
    boost::split(entries, command.substr(1), boost::is_any_of(";"));
    for (const auto& thread_action : entries) {
        std::vector<std::string> parts;
        boost::split(parts, thread_action, boost::is_any_of(":"));

        if (parts.size() == 1 && (parts[0] == "c" || parts[0].starts_with("C"))) {
            lock_execution = false;
        }
        if (parts.size() == 2 && (parts[0] == "s" || parts[0].starts_with("S"))) {
            stepped_thread = GetThreadByID(strtoll(parts[1].data(), nullptr, 16));
        }
    }

    if (stepped_thread) {
        backend.SetActiveThread(stepped_thread);
        actions.push_back(lock_execution ? DebuggerAction::StepThreadLocked
                                         : DebuggerAction::StepThreadUnlocked);
    } else {
        actions.push_back(DebuggerAction::Continue);
    }
}

constexpr std::array<std::pair<const char*, Kernel::Svc::MemoryState>, 22> MemoryStateNames{{
    {"----- Free ------", Kernel::Svc::MemoryState::Free},
    {"Io               ", Kernel::Svc::MemoryState::Io},
    {"Static           ", Kernel::Svc::MemoryState::Static},
    {"Code             ", Kernel::Svc::MemoryState::Code},
    {"CodeData         ", Kernel::Svc::MemoryState::CodeData},
    {"Normal           ", Kernel::Svc::MemoryState::Normal},
    {"Shared           ", Kernel::Svc::MemoryState::Shared},
    {"AliasCode        ", Kernel::Svc::MemoryState::AliasCode},
    {"AliasCodeData    ", Kernel::Svc::MemoryState::AliasCodeData},
    {"Ipc              ", Kernel::Svc::MemoryState::Ipc},
    {"Stack            ", Kernel::Svc::MemoryState::Stack},
    {"ThreadLocal      ", Kernel::Svc::MemoryState::ThreadLocal},
    {"Transferred      ", Kernel::Svc::MemoryState::Transferred},
    {"SharedTransferred", Kernel::Svc::MemoryState::SharedTransferred},
    {"SharedCode       ", Kernel::Svc::MemoryState::SharedCode},
    {"Inaccessible     ", Kernel::Svc::MemoryState::Inaccessible},
    {"NonSecureIpc     ", Kernel::Svc::MemoryState::NonSecureIpc},
    {"NonDeviceIpc     ", Kernel::Svc::MemoryState::NonDeviceIpc},
    {"Kernel           ", Kernel::Svc::MemoryState::Kernel},
    {"GeneratedCode    ", Kernel::Svc::MemoryState::GeneratedCode},
    {"CodeOut          ", Kernel::Svc::MemoryState::CodeOut},
    {"Coverage         ", Kernel::Svc::MemoryState::Coverage},
}};

static constexpr const char* GetMemoryStateName(Kernel::Svc::MemoryState state) {
    for (size_t i = 0; i < MemoryStateNames.size(); i++) {
        if (std::get<1>(MemoryStateNames[i]) == state) {
            return std::get<0>(MemoryStateNames[i]);
        }
    }
    return "Unknown         ";
}

static constexpr const char* GetMemoryPermissionString(const Kernel::Svc::MemoryInfo& info) {
    if (info.state == Kernel::Svc::MemoryState::Free) {
        return "   ";
    }

    switch (info.permission) {
    case Kernel::Svc::MemoryPermission::ReadExecute:
        return "r-x";
    case Kernel::Svc::MemoryPermission::Read:
        return "r--";
    case Kernel::Svc::MemoryPermission::ReadWrite:
        return "rw-";
    default:
        return "---";
    }
}

void GDBStub::HandleRcmd(const std::vector<u8>& command) {
    std::string_view command_str{reinterpret_cast<const char*>(&command[0]), command.size()};
    std::string reply;

    auto* process = GetProcess();
    auto& page_table = process->GetPageTable();

    const char* commands = "Commands:\n"
                           "  get fastmem\n"
                           "  get info\n"
                           "  get mappings\n";

    if (command_str == "get fastmem") {
        if (Settings::IsFastmemEnabled()) {
            const auto& impl = page_table.GetImpl();
            const auto region = reinterpret_cast<uintptr_t>(impl.fastmem_arena);
            const auto region_bits = impl.current_address_space_width_in_bits;
            const auto region_size = 1ULL << region_bits;

            reply = fmt::format("Region bits:  {}\n"
                                "Host address: {:#x} - {:#x}\n",
                                region_bits, region, region + region_size - 1);
        } else {
            reply = "Fastmem is not enabled.\n";
        }
    } else if (command_str == "get info") {
        auto modules = Core::FindModules(process);

        reply = fmt::format("Process:     {:#x} ({})\n"
                            "Program Id:  {:#018x}\n",
                            process->GetProcessId(), process->GetName(), process->GetProgramId());
        reply += fmt::format(
            "Layout:\n"
            "  Alias: {:#012x} - {:#012x}\n"
            "  Heap:  {:#012x} - {:#012x}\n"
            "  Aslr:  {:#012x} - {:#012x}\n"
            "  Stack: {:#012x} - {:#012x}\n"
            "Modules:\n",
            GetInteger(page_table.GetAliasRegionStart()),
            GetInteger(page_table.GetAliasRegionStart()) + page_table.GetAliasRegionSize() - 1,
            GetInteger(page_table.GetHeapRegionStart()),
            GetInteger(page_table.GetHeapRegionStart()) + page_table.GetHeapRegionSize() - 1,
            GetInteger(page_table.GetAliasCodeRegionStart()),
            GetInteger(page_table.GetAliasCodeRegionStart()) + page_table.GetAliasCodeRegionSize() -
                1,
            GetInteger(page_table.GetStackRegionStart()),
            GetInteger(page_table.GetStackRegionStart()) + page_table.GetStackRegionSize() - 1);

        for (const auto& [vaddr, name] : modules) {
            reply += fmt::format("  {:#012x} - {:#012x} {}\n", vaddr,
                                 GetInteger(Core::GetModuleEnd(process, vaddr)), name);
        }
    } else if (command_str == "get mappings") {
        reply = "Mappings:\n";
        VAddr cur_addr = 0;

        while (true) {
            using MemoryAttribute = Kernel::Svc::MemoryAttribute;

            Kernel::KMemoryInfo mem_info{};
            Kernel::Svc::PageInfo page_info{};
            R_ASSERT(page_table.QueryInfo(std::addressof(mem_info), std::addressof(page_info),
                                          cur_addr));
            auto svc_mem_info = mem_info.GetSvcMemoryInfo();

            if (svc_mem_info.state != Kernel::Svc::MemoryState::Inaccessible ||
                svc_mem_info.base_address + svc_mem_info.size - 1 !=
                    std::numeric_limits<u64>::max()) {
                const char* state = GetMemoryStateName(svc_mem_info.state);
                const char* perm = GetMemoryPermissionString(svc_mem_info);

                const char l = True(svc_mem_info.attribute & MemoryAttribute::Locked) ? 'L' : '-';
                const char i =
                    True(svc_mem_info.attribute & MemoryAttribute::IpcLocked) ? 'I' : '-';
                const char d =
                    True(svc_mem_info.attribute & MemoryAttribute::DeviceShared) ? 'D' : '-';
                const char u = True(svc_mem_info.attribute & MemoryAttribute::Uncached) ? 'U' : '-';
                const char p =
                    True(svc_mem_info.attribute & MemoryAttribute::PermissionLocked) ? 'P' : '-';

                reply += fmt::format(
                    "  {:#012x} - {:#012x} {} {} {}{}{}{}{} [{}, {}]\n", svc_mem_info.base_address,
                    svc_mem_info.base_address + svc_mem_info.size - 1, perm, state, l, i, d, u, p,
                    svc_mem_info.ipc_count, svc_mem_info.device_count);
            }

            const uintptr_t next_address = svc_mem_info.base_address + svc_mem_info.size;
            if (next_address <= cur_addr) {
                break;
            }

            cur_addr = next_address;
        }
    } else if (command_str == "help") {
        reply = commands;
    } else {
        reply = "Unknown command.\n";
        reply += commands;
    }

    std::span<const u8> reply_span{reinterpret_cast<u8*>(&reply.front()), reply.size()};
    SendReply(Common::HexToString(reply_span, false));
}

Kernel::KThread* GDBStub::GetThreadByID(u64 thread_id) {
    auto& threads{GetProcess()->GetThreadList()};
    for (auto& thread : threads) {
        if (thread.GetThreadId() == thread_id) {
            return std::addressof(thread);
        }
    }

    return nullptr;
}

std::vector<char>::const_iterator GDBStub::CommandEnd() const {
    // Find the end marker
    const auto end{std::find(current_command.begin(), current_command.end(), GDB_STUB_END)};

    // Require the checksum to be present
    return std::min(end + 2, current_command.end());
}

std::optional<std::string> GDBStub::DetachCommand() {
    // Slice the string part from the beginning to the end marker
    const auto end{CommandEnd()};

    // Extract possible command data
    std::string data(current_command.data(), end - current_command.begin() + 1);

    // Shift over the remaining contents
    current_command.erase(current_command.begin(), end + 1);

    // Validate received command
    if (data[0] != GDB_STUB_START) {
        LOG_ERROR(Debug_GDBStub, "Invalid start data: {}", data[0]);
        return std::nullopt;
    }

    u8 calculated = CalculateChecksum(std::string_view(data).substr(1, data.size() - 4));
    u8 received = static_cast<u8>(strtoll(data.data() + data.size() - 2, nullptr, 16));

    // Verify checksum
    if (calculated != received) {
        LOG_ERROR(Debug_GDBStub, "Checksum mismatch: calculated {:02x}, received {:02x}",
                  calculated, received);
        return std::nullopt;
    }

    return data.substr(1, data.size() - 4);
}

void GDBStub::SendReply(std::string_view data) {
    const auto escaped{EscapeGDB(data)};
    const auto output{fmt::format("{}{}{}{:02x}", GDB_STUB_START, escaped, GDB_STUB_END,
                                  CalculateChecksum(escaped))};
    LOG_TRACE(Debug_GDBStub, "Writing reply: {}", output);

    // C++ string support is complete rubbish
    const u8* output_begin = reinterpret_cast<const u8*>(output.data());
    const u8* output_end = output_begin + output.size();
    backend.WriteToClient(std::span<const u8>(output_begin, output_end));
}

void GDBStub::SendStatus(char status) {
    if (no_ack) {
        return;
    }

    std::array<u8, 1> buf = {static_cast<u8>(status)};
    LOG_TRACE(Debug_GDBStub, "Writing status: {}", status);
    backend.WriteToClient(buf);
}

Kernel::KProcess* GDBStub::GetProcess() {
    return debug_process;
}

Core::Memory::Memory& GDBStub::GetMemory() {
    return GetProcess()->GetMemory();
}

} // namespace Core
