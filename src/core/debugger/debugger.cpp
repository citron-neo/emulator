// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>
#if HAS_BOOST_PROCESS && !defined(__ANDROID__) && !defined(__APPLE__)
#include <boost/process/v1/async_pipe.hpp>
#endif

#include "common/logging/log.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/debugger/debugger.h"
#include "core/debugger/debugger_interface.h"
#include "core/debugger/gdbstub.h"
#include "core/hle/kernel/global_scheduler_context.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_scheduler.h"

template <typename Readable, typename Buffer, typename Callback>
static void AsyncReceiveInto(Readable& r, Buffer& buffer, Callback&& c) {
    static_assert(std::is_trivial_v<Buffer>);
    auto boost_buffer{boost::asio::buffer(&buffer, sizeof(Buffer))};
    r.async_read_some(
        boost_buffer, [&, c](const boost::system::error_code& error, size_t bytes_read) {
            if (!error.failed()) {
                const u8* buffer_start = reinterpret_cast<const u8*>(&buffer);
                std::span<const u8> received_data{buffer_start, buffer_start + bytes_read};
                c(received_data);
                AsyncReceiveInto(r, buffer, c);
            }
        });
}

template <typename Callback>
static void AsyncAccept(boost::asio::ip::tcp::acceptor& acceptor, Callback&& c) {
    acceptor.async_accept([&, c](const boost::system::error_code& error, auto&& peer_socket) {
        if (!error.failed()) {
            c(peer_socket);
            AsyncAccept(acceptor, c);
        }
    });
}

template <typename Readable, typename Buffer>
static std::span<const u8> ReceiveInto(Readable& r, Buffer& buffer) {
    static_assert(std::is_trivial_v<Buffer>);
    auto boost_buffer{boost::asio::buffer(&buffer, sizeof(Buffer))};
    size_t bytes_read = r.read_some(boost_buffer);
    const u8* buffer_start = reinterpret_cast<const u8*>(&buffer);
    std::span<const u8> received_data{buffer_start, buffer_start + bytes_read};
    return received_data;
}

enum class SignalType {
    Stopped,
    Watchpoint,
    ShuttingDown,
};

struct SignalInfo {
    SignalType type;
    Kernel::KThread* thread;
    const Kernel::DebugWatchpoint* watchpoint;
};

namespace Core {

class DebuggerImpl : public DebuggerBackend {
public:
    explicit DebuggerImpl(Core::System& system_, u16 port) : system{system_} {
        InitializeServer(port);
    }

    ~DebuggerImpl() override {
        ShutdownServer();
    }

    bool SignalDebugger(SignalInfo signal_info) {
        std::scoped_lock lk{connection_lock};

        if (stopped || !state) {
            // Do not notify the debugger about another event.
            // It should be ignored.
            return false;
        }

        // Set up the state.
        stopped = true;
        state->info = signal_info;

        // Write a single byte into the pipe to wake up the debug interface.
        boost::asio::write(state->signal_pipe, boost::asio::buffer(&stopped, sizeof(stopped)));

        return true;
    }

    // These functions are callbacks from the frontend, and the lock will be held.
    // There is no need to relock it.

    std::span<const u8> ReadFromClient() override {
        return ReceiveInto(state->client_socket, state->client_data);
    }

    void WriteToClient(std::span<const u8> data) override {
        boost::asio::write(state->client_socket,
                           boost::asio::buffer(data.data(), data.size_bytes()));
    }

    void SetActiveThread(Kernel::KThread* thread) override {
        state->active_thread = thread;
    }

    Kernel::KThread* GetActiveThread() override {
        return state->active_thread.GetPointerUnsafe();
    }

private:
    void InitializeServer(u16 port) {
        using boost::asio::ip::tcp;

        LOG_INFO(Debug_GDBStub, "Starting server on port {}...", port);

        // Run the connection thread.
        connection_thread = std::jthread([&, port](std::stop_token stop_token) {
            Common::SetCurrentThreadName("Debugger");

            try {
                // Initialize the listening socket and accept a new client.
                tcp::endpoint endpoint{boost::asio::ip::address_v4::any(), port};
                tcp::acceptor acceptor{io_context, endpoint};

                AsyncAccept(acceptor, [&](auto&& peer) { AcceptConnection(std::move(peer)); });

                while (!stop_token.stop_requested() && io_context.run()) {
                }
            } catch (const std::exception& ex) {
                LOG_CRITICAL(Debug_GDBStub, "Stopping server: {}", ex.what());
            }
        });
    }

    void AcceptConnection(boost::asio::ip::tcp::socket&& peer) {
        LOG_INFO(Debug_GDBStub, "Accepting new peer connection");

        std::scoped_lock lk{connection_lock};

        // Find the process we are going to debug.
        SetDebugProcess();

        // Ensure everything is stopped.
        PauseEmulation();

        // Set up the new frontend.
        frontend = std::make_unique<GDBStub>(*this, system, debug_process.GetPointerUnsafe());

        // Set the new state. This will tear down any existing state.
        state = ConnectionState{
            .client_socket{std::move(peer)},
            #if HAS_BOOST_PROCESS && !defined(__ANDROID__) && !defined(__APPLE__)
            .signal_pipe = boost::process::v1::async_pipe(io_context),
            #else
            // Use a regular socket pair for Android, macOS, or if boost_process is not found
            .signal_pipe = boost::asio::ip::tcp::socket(io_context),
            #endif
            .info{},
            .active_thread{},
            .client_data{},
            .pipe_data{},
        };

        // Set up the client signals for new data.
        AsyncReceiveInto(state->signal_pipe, state->pipe_data, [&](auto d) { PipeData(d); });
        AsyncReceiveInto(state->client_socket, state->client_data, [&](auto d) { ClientData(d); });

        // Set the active thread.
        UpdateActiveThread();

        // Set up the frontend.
        frontend->Connected();
    }

    void ShutdownServer() {
        connection_thread.request_stop();
        io_context.stop();
        connection_thread.join();
    }

    void PipeData(std::span<const u8> data) {
        std::scoped_lock lk{connection_lock};

        switch (state->info.type) {
        case SignalType::Stopped:
        case SignalType::Watchpoint:
            // Stop emulation.
            PauseEmulation();

            // Notify the client.
            state->active_thread = state->info.thread;
            UpdateActiveThread();

            if (state->info.type == SignalType::Watchpoint) {
                frontend->Watchpoint(std::addressof(*state->active_thread),
                                     *state->info.watchpoint);
            } else {
                frontend->Stopped(std::addressof(*state->active_thread));
            }

            break;
        case SignalType::ShuttingDown:
            frontend->ShuttingDown();

            // Release members.
            state->active_thread.Reset(nullptr);
            debug_process.Reset(nullptr);

            // Wait for emulation to shut down gracefully now.
            state->signal_pipe.close();
            state->client_socket.shutdown(boost::asio::socket_base::shutdown_both);
            LOG_INFO(Debug_GDBStub, "Shut down server");

            break;
        }
    }

    void ClientData(std::span<const u8> data) {
        std::scoped_lock lk{connection_lock};

        const auto actions{frontend->ClientData(data)};
        for (const auto action : actions) {
            switch (action) {
            case DebuggerAction::Interrupt: {
                stopped = true;
                PauseEmulation();
                UpdateActiveThread();
                frontend->Stopped(state->active_thread.GetPointerUnsafe());
                break;
            }
            case DebuggerAction::Continue:
                MarkResumed([&] { ResumeEmulation(); });
                break;
            case DebuggerAction::StepThreadUnlocked:
                MarkResumed([&] {
                    state->active_thread->SetStepState(Kernel::StepState::StepPending);
                    state->active_thread->Resume(Kernel::SuspendType::Debug);
                    ResumeEmulation(state->active_thread.GetPointerUnsafe());
                });
                break;
            case DebuggerAction::StepThreadLocked: {
                MarkResumed([&] {
                    state->active_thread->SetStepState(Kernel::StepState::StepPending);
                    state->active_thread->Resume(Kernel::SuspendType::Debug);
                });
                break;
            }
            case DebuggerAction::ShutdownEmulation: {
                // Spawn another thread that will exit after shutdown,
                // to avoid a deadlock
                Core::System* system_ref{&system};
                std::thread t([system_ref] { system_ref->Exit(); });
                t.detach();
                break;
            }
            }
        }
    }

    void PauseEmulation() {
        Kernel::KScopedLightLock ll{debug_process->GetListLock()};
        Kernel::KScopedSchedulerLock sl{system.Kernel()};

        // Put all threads to sleep on next scheduler round.
        for (auto& thread : ThreadList()) {
            thread.RequestSuspend(Kernel::SuspendType::Debug);
        }
    }

    void ResumeEmulation(Kernel::KThread* except = nullptr) {
        Kernel::KScopedLightLock ll{debug_process->GetListLock()};
        Kernel::KScopedSchedulerLock sl{system.Kernel()};

        // Wake up all threads.
        for (auto& thread : ThreadList()) {
            if (std::addressof(thread) == except) {
                continue;
            }

            thread.SetStepState(Kernel::StepState::NotStepping);
            thread.Resume(Kernel::SuspendType::Debug);
        }
    }

    template <typename Callback>
    void MarkResumed(Callback&& cb) {
        stopped = false;
        cb();
    }

    void UpdateActiveThread() {
        Kernel::KScopedLightLock ll{debug_process->GetListLock()};

        auto& threads{ThreadList()};
        for (auto& thread : threads) {
            if (std::addressof(thread) == state->active_thread.GetPointerUnsafe()) {
                // Thread is still alive, no need to update.
                return;
            }
        }
        state->active_thread = std::addressof(threads.front());
    }

private:
    void SetDebugProcess() {
        debug_process = std::move(system.Kernel().GetProcessList().back());
    }

    Kernel::KProcess::ThreadList& ThreadList() {
        return debug_process->GetThreadList();
    }

private:
    System& system;
    Kernel::KScopedAutoObject<Kernel::KProcess> debug_process;
    std::unique_ptr<DebuggerFrontend> frontend;

    boost::asio::io_context io_context;
    std::jthread connection_thread;
    std::mutex connection_lock;

    struct ConnectionState {
        boost::asio::ip::tcp::socket client_socket;
        #if HAS_BOOST_PROCESS && !defined(__ANDROID__) && !defined(__APPLE__)
        boost::process::v1::async_pipe signal_pipe;
        #else
        // Use a regular socket pair for Android, macOS, or if boost_process is not found
        boost::asio::ip::tcp::socket signal_pipe;
        #endif

        SignalInfo info;
        Kernel::KScopedAutoObject<Kernel::KThread> active_thread;
        std::array<u8, 4096> client_data;
        bool pipe_data;
    };

    std::optional<ConnectionState> state{};
    bool stopped{};
};

Debugger::Debugger(Core::System& system, u16 port) {
    try {
        impl = std::make_unique<DebuggerImpl>(system, port);
    } catch (const std::exception& ex) {
        LOG_CRITICAL(Debug_GDBStub, "Failed to initialize debugger: {}", ex.what());
    }
}

Debugger::~Debugger() = default;

bool Debugger::NotifyThreadStopped(Kernel::KThread* thread) {
    return impl && impl->SignalDebugger(SignalInfo{SignalType::Stopped, thread, nullptr});
}

bool Debugger::NotifyThreadWatchpoint(Kernel::KThread* thread,
                                      const Kernel::DebugWatchpoint& watch) {
    return impl && impl->SignalDebugger(SignalInfo{SignalType::Watchpoint, thread, &watch});
}

void Debugger::NotifyShutdown() {
    if (impl) {
        impl->SignalDebugger(SignalInfo{SignalType::ShuttingDown, nullptr, nullptr});
    }
}

} // namespace Core
