// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <android/native_window_jni.h>
#include "common/android/applets/software_keyboard.h"
#include "common/detached_tasks.h"
#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/perf_stats.h"
#include "frontend_common/content_manager.h"
#include "jni/emu_window/emu_window.h"
#include "video_core/rasterizer_interface.h"

#pragma once

class EmulationSession final {
public:
    explicit EmulationSession();
    ~EmulationSession();

    static EmulationSession& GetInstance();
    const Core::System& System() const;
    Core::System& System();
    FileSys::ManualContentProvider* GetContentProvider();
    InputCommon::InputSubsystem& GetInputSubsystem();

    const EmuWindow_Android& Window() const;
    EmuWindow_Android& Window();
    void SetNativeWindow(ANativeWindow* native_window);
    void SurfaceChanged();

    void InitializeGpuDriver(const std::string& hook_lib_dir, const std::string& custom_driver_dir,
                             const std::string& custom_driver_name,
                             const std::string& file_redirect_dir);

    bool IsRunning() const;
    bool IsPaused() const;
    bool IsShuttingDown() const;
    bool IsNetworkInitialized() const;
    [[nodiscard]] std::unique_lock<std::mutex> AcquireSessionLock();
    void PauseEmulation();
    void UnPauseEmulation();
    void HaltEmulation();
    void RunEmulation();
    void ShutdownEmulation();

    const Core::PerfStatsResults& PerfStats();
    void ConfigureFilesystemProvider(const std::string& filepath);
    void InitializeSystem(bool reload);
    void RefreshContentSystem();
    bool RefreshContentIfIdle(bool keys_loaded);
    void SetFilesystemInitStage(Service::FileSystem::InitStage stage);
    void PromoteFilesystemInitStage(Service::FileSystem::InitStage stage);
    void SetAppletId(int applet_id);
    Core::SystemResultStatus InitializeEmulation(const std::string& filepath,
                                                 const std::size_t program_index,
                                                 const bool frontend_initiated);

    Common::Android::SoftwareKeyboard::AndroidKeyboard* SoftwareKeyboard();

    static void OnEmulationStarted();

    static u64 GetProgramId(JNIEnv* env, jstring jprogramId);

private:
    void RefreshContentSystemUnlocked();
    static void LoadDiskCacheProgress(VideoCore::LoadCallbackStage stage, int progress, int max);
    static void OnEmulationStopped(Core::SystemResultStatus result);
    static void ChangeProgram(std::size_t program_index);

private:
    // Window management
    std::unique_ptr<EmuWindow_Android> m_window;
    ANativeWindow* m_native_window{};
    mutable std::mutex m_window_mutex;

    // Core emulation
    Core::System m_system;
    InputCommon::InputSubsystem m_input_subsystem;
    Common::DetachedTasks m_detached_tasks;
    Core::PerfStatsResults m_perf_stats{};
    std::shared_ptr<FileSys::VfsFilesystem> m_vfs;
    Core::SystemResultStatus m_load_result{Core::SystemResultStatus::ErrorNotInitialized};
    std::atomic<bool> m_is_running = false;
    std::atomic<bool> m_is_paused = false;
    std::atomic<bool> m_is_shutting_down = false;
    bool m_session_active = false;
    bool m_network_initialized{};
    Common::Android::SoftwareKeyboard::AndroidKeyboard* m_software_keyboard{};
    std::unique_ptr<FileSys::ManualContentProvider> m_manual_provider;
    int m_applet_id{1};

    // GPU driver parameters
    std::shared_ptr<Common::DynamicLibrary> m_vulkan_library;

    // Synchronization
    std::condition_variable_any m_cv;
    std::condition_variable m_session_cv;
    mutable std::mutex m_mutex;

    // Program index for next boot
    std::atomic<s32> m_next_program_index = -1;
};
