// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "common/detached_tasks.h"
#include "core/core.h"
#include "core/perf_stats.h"
#include "frontend_common/content_manager.h"
#include "input_common/main.h"
#include "ios/emu_window/emu_window.h"
#include "video_core/rasterizer_interface.h"

namespace IOS {

using LifecycleCallback = void (*)(int result);

class EmulationSession final {
public:
    EmulationSession();
    ~EmulationSession();

    static EmulationSession& GetInstance();

    Core::System& System();
    InputCommon::InputSubsystem& GetInputSubsystem();

    bool IsRunning() const;
    bool IsPaused() const;

    void Initialize(const std::string& app_directory);
    void SetNativeLayer(void* metal_layer, int width, int height, float scale);
    Core::SystemResultStatus Launch(const std::string& filepath, std::size_t program_index);
    void Pause();
    void Resume();
    void Stop();
    void Shutdown();

    void TouchPressed(int id, float x, float y);
    void TouchMoved(int id, float x, float y);
    void TouchReleased(int id);
    void SetButtonState(std::size_t player_index, int button_id, bool pressed);
    void SetStickPosition(std::size_t player_index, int stick_id, float x, float y);
    void NotifyEmulationStarted();

    void SetCallbacks(LifecycleCallback started, LifecycleCallback stopped);

private:
    void ConfigureFilesystemProvider(const std::string& filepath);
    void InitializeSystem(bool reload);
    Core::SystemResultStatus InitializeEmulation(const std::string& filepath,
                                                 std::size_t program_index);
    void RunEmulation();
    void ShutdownEmulation(Core::SystemResultStatus result);

    static void LoadDiskCacheProgress(VideoCore::LoadCallbackStage stage, int progress, int max);

    std::unique_ptr<EmuWindow_IOS> window;
    void* native_layer{};
    int surface_width{};
    int surface_height{};
    float surface_scale{1.0f};

    Core::System system;
    InputCommon::InputSubsystem input_subsystem;
    Common::DetachedTasks detached_tasks;
    std::shared_ptr<FileSys::VfsFilesystem> vfs;
    std::unique_ptr<FileSys::ManualContentProvider> manual_provider;
    Core::SystemResultStatus load_result{Core::SystemResultStatus::ErrorNotInitialized};

    std::atomic<bool> is_initialized = false;
    std::atomic<bool> is_running = false;
    std::atomic<bool> is_paused = false;
    std::condition_variable_any cv;
    mutable std::mutex mutex;
    std::thread emulation_thread;

    LifecycleCallback on_started{};
    LifecycleCallback on_stopped{};
};

} // namespace IOS

extern "C" {
void citron_ios_initialize(const char* app_directory);
void citron_ios_set_callbacks(IOS::LifecycleCallback started, IOS::LifecycleCallback stopped);
void citron_ios_set_metal_layer(void* metal_layer, int width, int height, float scale);
int citron_ios_launch_game(const char* filepath, int program_index);
void citron_ios_pause();
void citron_ios_resume();
void citron_ios_stop();
void citron_ios_shutdown();
bool citron_ios_is_running();
bool citron_ios_is_paused();
void citron_ios_touch_began(int id, float x, float y);
void citron_ios_touch_moved(int id, float x, float y);
void citron_ios_touch_ended(int id);
void citron_ios_set_button(int player_index, int button_id, bool pressed);
void citron_ios_set_stick(int player_index, int stick_id, float x, float y);
bool citron_ios_jit_available();
}
