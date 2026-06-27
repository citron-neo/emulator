// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ios/native.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sys/mman.h>
#include <thread>
#include <chrono>

#if defined(__APPLE__)
#    include <TargetConditionals.h>
#    if TARGET_OS_IPHONE
#        include <dlfcn.h>
#        include <mach-o/dyld.h>
#    endif
#endif

#include "common/fs/path_util.h"
#include "common/fs/fs.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/crypto/key_manager.h"
#include "core/cpu_manager.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/frontend/applets/cabinet.h"
#include "core/frontend/applets/controller.h"
#include "core/frontend/applets/error.h"
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/profile_select.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "video_core/renderer_base.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_notify.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE
#    include <cstring>
#    include <pthread/pthread.h>

extern "C" {
// Set from citron_ios_initialize on the main thread; oaknut uses this before dlsym on guest threads.
void (*citron_ios_pthread_jit_write_protect_np)(int) = nullptr;
// iOS 17.4+: pthread_jit_write_with_callback_np (pthread_jit_write_protect_np is __API_UNAVAILABLE(ios)).
int (*citron_ios_pthread_jit_write_with_callback_np)(int (*)(void*), void*) = nullptr;
// Crash anonymized exports often copy this into the report's "Application Specific Information".
const char* __crashreporter_info__ = nullptr;
}

static char g_oaknut_jit_fault_note[512];

namespace {
/**
 * iOS libSystem re-exports pthread_jit_write_freeze_callbacks_np, pthread_jit_write_protect_supported_np,
 * and pthread_jit_write_with_callback_np — but not pthread_jit_write_protect_np (it appears only in the
 * macOS libSystem stub). Apple documents iOS JIT writers should use pthread_jit_write_with_callback_np
 * plus (on some distributions) com.apple.security.cs.jit-write-allowlist; sideload profiles often omit it.
 * Without that entitlement, pthread_jit_write_with_callback_np still toggles the MAP_JIT region per SDK docs.
 * Dynarmic's Oaknut backend expects the macOS-style protect/unprotect model, so dlsym here is often NULL
 * on device; raw mprotect(PROT_EXEC) on MAP_JIT / RW pages then fails with EACCES (W^X).
 */
void CitronIosResolveJitWriteProtectPointer() {
    static std::once_flag resolve_once;
    std::call_once(resolve_once, [] {
    static const char* const kSystemLibs[] = {
        "/usr/lib/system/libsystem_pthread.dylib",
        "/usr/lib/libSystem.B.dylib",
    };

    if (citron_ios_pthread_jit_write_with_callback_np == nullptr) {
        if (__builtin_available(iOS 17.4, *)) {
            citron_ios_pthread_jit_write_with_callback_np = pthread_jit_write_with_callback_np;
        }
    }
    // Weak-linked SDK symbol can be null at load when deployment target < 17.4; dlsym still resolves on device.
    if (citron_ios_pthread_jit_write_with_callback_np == nullptr) {
        using CbFn = int (*)(int (*)(void*), void*);
        const char* const cb_syms[] = {"pthread_jit_write_with_callback_np", "_pthread_jit_write_with_callback_np"};
        bool resolved = false;
        for (const char* sym : cb_syms) {
            for (void* handle :
                 {static_cast<void*>(RTLD_DEFAULT), static_cast<void*>(RTLD_NEXT),
#if defined(RTLD_MAIN_ONLY)
                  static_cast<void*>(RTLD_MAIN_ONLY),
#endif
#if defined(RTLD_SELF)
                  static_cast<void*>(RTLD_SELF),
#endif
                 }) {
                if (void* const p = dlsym(handle, sym)) {
                    citron_ios_pthread_jit_write_with_callback_np = reinterpret_cast<CbFn>(p);
                    resolved = true;
                    break;
                }
            }
            if (resolved) {
                break;
            }
        }
        if (!resolved) {
            for (const char* lib : kSystemLibs) {
                void* const h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
                if (h == nullptr) {
                    continue;
                }
                for (const char* sym : cb_syms) {
                    if (void* const p = dlsym(h, sym)) {
                        citron_ios_pthread_jit_write_with_callback_np = reinterpret_cast<CbFn>(p);
                        resolved = true;
                        break;
                    }
                }
                if (resolved) {
                    break;
                }
            }
        }
        if (!resolved) {
            const uint32_t image_count = _dyld_image_count();
            for (uint32_t i = 0; i < image_count; ++i) {
                const char* const image_path = _dyld_get_image_name(i);
                if (image_path == nullptr) {
                    continue;
                }
                void* const mh = dlopen(image_path, RTLD_NOW | RTLD_NOLOAD);
                if (mh == nullptr) {
                    continue;
                }
                for (const char* sym : cb_syms) {
                    if (void* const p = dlsym(mh, sym)) {
                        citron_ios_pthread_jit_write_with_callback_np = reinterpret_cast<CbFn>(p);
                        resolved = true;
                        break;
                    }
                }
                if (resolved) {
                    break;
                }
            }
        }
    }

    if (citron_ios_pthread_jit_write_protect_np != nullptr) {
        return;
    }
    using Fn = void (*)(int);
    const char* const syms[] = {"pthread_jit_write_protect_np", "_pthread_jit_write_protect_np"};
    for (const char* sym : syms) {
        for (void* handle :
             {static_cast<void*>(RTLD_DEFAULT), static_cast<void*>(RTLD_NEXT),
#if defined(RTLD_MAIN_ONLY)
              static_cast<void*>(RTLD_MAIN_ONLY),
#endif
#if defined(RTLD_SELF)
              static_cast<void*>(RTLD_SELF),
#endif
             }) {
            if (void* const p = dlsym(handle, sym)) {
                citron_ios_pthread_jit_write_protect_np = reinterpret_cast<Fn>(p);
                return;
            }
        }
    }
    for (const char* lib : kSystemLibs) {
        void* const h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr) {
            continue;
        }
        for (const char* sym : syms) {
            if (void* const p = dlsym(h, sym)) {
                citron_ios_pthread_jit_write_protect_np = reinterpret_cast<Fn>(p);
                return;
            }
        }
    }

    const uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; ++i) {
        const char* const image_path = _dyld_get_image_name(i);
        if (image_path == nullptr) {
            continue;
        }
        void* const mh = dlopen(image_path, RTLD_NOW | RTLD_NOLOAD);
        if (mh == nullptr) {
            continue;
        }
        for (const char* sym : syms) {
            if (void* const p = dlsym(mh, sym)) {
                citron_ios_pthread_jit_write_protect_np = reinterpret_cast<Fn>(p);
                return;
            }
        }
    }
    });
}
} // namespace

/** Called from oaknut before using JIT pthread pointers (guest CPU threads may run before main-thread init order settles). */
extern "C" void citron_ios_ensure_jit_apis_resolved(void)
{
    CitronIosResolveJitWriteProtectPointer();
}

extern "C" void citron_ios_note_oaknut_jit_fault(const char* message)
{
    if (message == nullptr) {
        return;
    }
    std::strncpy(g_oaknut_jit_fault_note, message, sizeof(g_oaknut_jit_fault_note) - 1);
    g_oaknut_jit_fault_note[sizeof(g_oaknut_jit_fault_note) - 1] = '\0';
    __crashreporter_info__ = g_oaknut_jit_fault_note;
}
#endif

namespace IOS {
namespace {
EmulationSession instance;

std::string SafeString(const char* value) {
    return value ? std::string{value} : std::string{};
}

void SetIOSCitronPath(Common::FS::CitronPath path_id, const std::filesystem::path& path) {
    if (Common::FS::CreateDirs(path)) {
        Common::FS::SetCitronPath(path_id, path);
    }
}

void ConfigureIOSAppDirectories(const std::string& app_directory) {
    const std::filesystem::path documents = app_directory;
    const std::filesystem::path citron = documents / "citron";

    SetIOSCitronPath(Common::FS::CitronPath::CitronDir, citron);
    SetIOSCitronPath(Common::FS::CitronPath::AmiiboDir, citron / "amiibo");
    SetIOSCitronPath(Common::FS::CitronPath::CacheDir, citron / "cache");
    SetIOSCitronPath(Common::FS::CitronPath::ConfigDir, citron / "config");
    SetIOSCitronPath(Common::FS::CitronPath::CrashDumpsDir, citron / "crash_dumps");
    SetIOSCitronPath(Common::FS::CitronPath::DumpDir, citron / "dump");
    SetIOSCitronPath(Common::FS::CitronPath::KeysDir, citron / "keys");
    SetIOSCitronPath(Common::FS::CitronPath::LoadDir, citron / "load");
    SetIOSCitronPath(Common::FS::CitronPath::LogDir, citron / "log");
    SetIOSCitronPath(Common::FS::CitronPath::NANDDir, citron / "nand");
    SetIOSCitronPath(Common::FS::CitronPath::PlayTimeDir, citron / "play_time");
    SetIOSCitronPath(Common::FS::CitronPath::ScreenshotsDir, citron / "screenshots");
    SetIOSCitronPath(Common::FS::CitronPath::SDMCDir, citron / "sdmc");
    SetIOSCitronPath(Common::FS::CitronPath::ShaderDir, citron / "shader");
    SetIOSCitronPath(Common::FS::CitronPath::TASDir, citron / "tas");
    SetIOSCitronPath(Common::FS::CitronPath::IconsDir, citron / "icons");
}

void ConfigureIOSRuntimeSettings() {
    Settings::values.sink_id.SetValue(Settings::AudioEngine::Auto);
    Settings::values.audio_output_device_id.SetValue("auto");
    Settings::values.audio_input_device_id.SetValue("null");
    Settings::values.log_filter.SetValue("*:Info Render.Vulkan:Debug HW.GPU:Debug");

#if defined(__APPLE__)
    setenv("MVK_CONFIG_LOG_LEVEL", "2", 0);
#endif

    // The iOS simulator's MoltenVK device exposes a much smaller feature set than real Apple GPUs.
    Settings::values.use_multi_core.SetValue(false);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(false);
    Settings::values.accelerate_astc.SetValue(Settings::AstcDecodeMode::Cpu);
    Settings::values.nvdec_emulation.SetValue(Settings::NvdecEmulation::Cpu);
    Settings::values.async_presentation.SetValue(false);
    Settings::values.use_reactive_flushing.SetValue(false);
    Settings::values.use_fast_gpu_time.SetValue(false);
    Settings::values.use_vulkan_driver_pipeline_cache.SetValue(false);
}
} // namespace

EmulationSession::EmulationSession() : vfs{std::make_shared<FileSys::RealVfsFilesystem>()} {}

EmulationSession::~EmulationSession() {
    Shutdown();
}

EmulationSession& EmulationSession::GetInstance() {
    return instance;
}

Core::System& EmulationSession::System() {
    return system;
}

InputCommon::InputSubsystem& EmulationSession::GetInputSubsystem() {
    return input_subsystem;
}

bool EmulationSession::IsRunning() const {
    return is_running;
}

bool EmulationSession::IsPaused() const {
    return is_running && is_paused;
}

void EmulationSession::SetCallbacks(LifecycleCallback started, LifecycleCallback stopped) {
    std::scoped_lock lock{mutex};
    on_started = started;
    on_stopped = stopped;
}

void EmulationSession::Initialize(const std::string& app_directory) {
    std::scoped_lock lock{mutex};
    if (is_initialized) {
        return;
    }

    Common::FS::SetAppDirectory(app_directory);
    ConfigureIOSAppDirectories(app_directory);
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::Start();

    Core::Crypto::KeyManager::Instance().ReloadKeys();
    LOG_INFO(Frontend, "iOS keys directory: {}",
             Common::FS::GetCitronPathString(Common::FS::CitronPath::KeysDir));
    LOG_INFO(Frontend, "iOS key files: prod.keys={}, title.keys={}",
             Core::Crypto::KeyManager::KeyFileExists(false),
             Core::Crypto::KeyManager::KeyFileExists(true));

    input_subsystem.Initialize();
    system.SetFilesystem(vfs);

    ConfigureIOSRuntimeSettings();
    is_initialized = true;
}

void EmulationSession::SetNativeLayer(void* metal_layer, int width, int height, float scale) {
    std::scoped_lock lock{mutex};
    native_layer = metal_layer;
    surface_width = width;
    surface_height = height;
    surface_scale = scale;

    if (window) {
        window->OnSurfaceChanged(native_layer, surface_width, surface_height, surface_scale);
    }
}

void EmulationSession::ConfigureFilesystemProvider(const std::string& filepath) {
    const auto file = system.GetFilesystem()->OpenFile(filepath, FileSys::OpenMode::Read);
    if (!file) {
        return;
    }

    auto loader = Loader::GetLoader(system, file);
    if (!loader) {
        return;
    }

    const auto file_type = loader->GetFileType();
    if (file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) {
        return;
    }

    u64 program_id = 0;
    const auto result = loader->ReadProgramId(program_id);
    if (result == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
        manual_provider->AddEntry(FileSys::TitleType::Application,
                                  FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                  program_id, file);
    } else if (result == Loader::ResultStatus::Success &&
               (file_type == Loader::FileType::XCI || file_type == Loader::FileType::NSP)) {
        const auto nsp = file_type == Loader::FileType::NSP
                             ? std::make_shared<FileSys::NSP>(file)
                             : FileSys::XCI{file}.GetSecurePartitionNSP();
        for (const auto& title : nsp->GetNCAs()) {
            for (const auto& entry : title.second) {
                manual_provider->AddEntry(entry.first.first, entry.first.second, title.first,
                                          entry.second->GetBaseFile());
            }
        }
    }
}

void EmulationSession::InitializeSystem(bool reload) {
    if (!reload) {
        system.SetFilesystem(vfs);
    }

    system.GetUserChannel().clear();
    manual_provider = std::make_unique<FileSys::ManualContentProvider>();
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual,
                                   manual_provider.get());
    system.GetFileSystemController().CreateFactories(*vfs);
}

Core::SystemResultStatus EmulationSession::InitializeEmulation(const std::string& filepath,
                                                               std::size_t program_index) {
    std::scoped_lock lock{mutex};
    if (!native_layer) {
        return Core::SystemResultStatus::ErrorVideoCore;
    }

    window = std::make_unique<EmuWindow_IOS>(native_layer, surface_width, surface_height,
                                             surface_scale);

    system.SetShuttingDown(false);
    system.ApplySettings();
    Settings::LogSettings();
    system.HIDCore().ReloadInputDevices();
    system.SetFrontendAppletSet({
        nullptr, // Amiibo Settings
        nullptr, // Controller Selector
        nullptr, // Error Display
        nullptr, // Mii Editor
        nullptr, // Parental Controls
        nullptr, // Photo Viewer
        nullptr, // Profile Selector
        nullptr, // Software Keyboard
        nullptr, // Web Browser
    });

    ConfigureFilesystemProvider(filepath);

    Service::AM::FrontendAppletParameters params{
        .applet_id = Service::AM::AppletId::Application,
        .launch_type = Service::AM::LaunchType::FrontendInitiated,
        .program_index = static_cast<s32>(program_index),
    };
    load_result = system.Load(*window, filepath, params);
    if (load_result != Core::SystemResultStatus::Success) {
        return load_result;
    }

    system.GPU().Start();
    system.GetCpuManager().OnGpuReady();
    system.RegisterExitCallback([this] {
        std::scoped_lock callback_lock{mutex};
        is_running = false;
        cv.notify_one();
    });
    return Core::SystemResultStatus::Success;
}

Core::SystemResultStatus EmulationSession::Launch(const std::string& filepath,
                                                  std::size_t program_index) {
    Stop();
    InitializeSystem(false);

    const auto result = InitializeEmulation(filepath, program_index);
    if (result != Core::SystemResultStatus::Success) {
        ShutdownEmulation(result);
        return result;
    }

    {
        std::scoped_lock lock{mutex};
        is_running = true;
        is_paused = false;
    }
    emulation_thread = std::thread{[this] { RunEmulation(); }};
    return result;
}

void EmulationSession::Pause() {
    std::scoped_lock lock{mutex};
    if (!is_running) {
        return;
    }
    system.Pause();
    is_paused = true;
}

void EmulationSession::Resume() {
    std::scoped_lock lock{mutex};
    if (!is_running) {
        return;
    }
    system.Run();
    is_paused = false;
}

void EmulationSession::Stop() {
    {
        std::scoped_lock lock{mutex};
        if (!is_running && !emulation_thread.joinable()) {
            return;
        }
        is_running = false;
        cv.notify_one();
    }

    if (emulation_thread.joinable()) {
        emulation_thread.join();
    }
}

void EmulationSession::Shutdown() {
    Stop();
    std::scoped_lock lock{mutex};
    if (is_initialized) {
        input_subsystem.Shutdown();
        Common::Log::Stop();
        is_initialized = false;
    }
}

void EmulationSession::RunEmulation() {
    if (Settings::values.use_disk_shader_cache.GetValue()) {
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
        system.Renderer().ReadRasterizer()->LoadDiskResources(
            system.GetApplicationProcessProgramID(), std::stop_token{}, LoadDiskCacheProgress);
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Complete, 0, 0);
    }

    void(system.Run());

    if (system.DebuggerEnabled()) {
        system.InitializeDebugger();
    }

    while (true) {
        std::unique_lock lock{mutex};
        if (cv.wait_for(lock, std::chrono::milliseconds(800), [this] { return !is_running; })) {
            break;
        }
    }

    ShutdownEmulation(Core::SystemResultStatus::Success);
}

void EmulationSession::ShutdownEmulation(Core::SystemResultStatus result) {
    std::scoped_lock lock{mutex};
    is_running = false;
    is_paused = false;

    system.HIDCore().UnloadInputDevices();
    system.HIDCore().SetSupportedStyleTag({Core::HID::NpadStyleSet::All});

    if (load_result == Core::SystemResultStatus::Success) {
        system.DetachDebugger();
        system.ShutdownMainProcess();
        detached_tasks.WaitForAllTasks();
        load_result = Core::SystemResultStatus::ErrorNotInitialized;
    }

    window.reset();
    if (on_stopped) {
        on_stopped(static_cast<int>(result));
    }
}

void EmulationSession::TouchPressed(int id, float x, float y) {
    std::scoped_lock lock{mutex};
    if (window) {
        window->OnTouchPressed(id, x, y);
    }
}

void EmulationSession::TouchMoved(int id, float x, float y) {
    std::scoped_lock lock{mutex};
    if (window) {
        window->OnTouchMoved(id, x, y);
    }
}

void EmulationSession::TouchReleased(int id) {
    std::scoped_lock lock{mutex};
    if (window) {
        window->OnTouchReleased(id);
    }
}

void EmulationSession::SetButtonState(std::size_t player_index, int button_id, bool pressed) {
    input_subsystem.GetVirtualGamepad()->SetButtonState(player_index, button_id, pressed);
}

void EmulationSession::SetStickPosition(std::size_t player_index, int stick_id, float x, float y) {
    input_subsystem.GetVirtualGamepad()->SetStickPosition(player_index, stick_id, x, y);
}

void EmulationSession::NotifyEmulationStarted() {
    if (on_started) {
        on_started(static_cast<int>(Core::SystemResultStatus::Success));
    }
}

void EmulationSession::LoadDiskCacheProgress(VideoCore::LoadCallbackStage, int, int) {}

} // namespace IOS

extern "C" {

void citron_ios_initialize(const char* app_directory) {
    IOS::EmulationSession::GetInstance().Initialize(IOS::SafeString(app_directory));
#if defined(__APPLE__) && TARGET_OS_IPHONE
    CitronIosResolveJitWriteProtectPointer();
#    if !TARGET_OS_SIMULATOR
    if (citron_ios_pthread_jit_write_protect_np == nullptr &&
        citron_ios_pthread_jit_write_with_callback_np == nullptr) {
        LOG_WARNING(Service_JIT,
                    "No iOS JIT pthread API resolved (pthread_jit_write_with_callback_np link failed).");
    }
#    endif
#endif
}

void citron_ios_set_callbacks(IOS::LifecycleCallback started, IOS::LifecycleCallback stopped) {
    IOS::EmulationSession::GetInstance().SetCallbacks(started, stopped);
}

void citron_ios_set_metal_layer(void* metal_layer, int width, int height, float scale) {
    IOS::EmulationSession::GetInstance().SetNativeLayer(metal_layer, width, height, scale);
}

int citron_ios_launch_game(const char* filepath, int program_index) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    CitronIosResolveJitWriteProtectPointer();
#    if !defined(HAS_NCE)
    if (citron_ios_pthread_jit_write_protect_np == nullptr &&
        citron_ios_pthread_jit_write_with_callback_np == nullptr) {
        LOG_ERROR(Service_JIT,
                  "Launch blocked: no JIT pthread API (need pthread_jit_write_with_callback_np on iOS).");
        return 30;
    }
#    endif
#endif
    const auto result = IOS::EmulationSession::GetInstance().Launch(
        IOS::SafeString(filepath), static_cast<std::size_t>(std::max(program_index, 0)));
    return static_cast<int>(result);
}

void citron_ios_pause() {
    IOS::EmulationSession::GetInstance().Pause();
}

void citron_ios_resume() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    CitronIosResolveJitWriteProtectPointer();
#endif
    IOS::EmulationSession::GetInstance().Resume();
}

void citron_ios_stop() {
    IOS::EmulationSession::GetInstance().Stop();
}

void citron_ios_shutdown() {
    IOS::EmulationSession::GetInstance().Shutdown();
}

bool citron_ios_is_running() {
    return IOS::EmulationSession::GetInstance().IsRunning();
}

bool citron_ios_is_paused() {
    return IOS::EmulationSession::GetInstance().IsPaused();
}

void citron_ios_touch_began(int id, float x, float y) {
    IOS::EmulationSession::GetInstance().TouchPressed(id, x, y);
}

void citron_ios_touch_moved(int id, float x, float y) {
    IOS::EmulationSession::GetInstance().TouchMoved(id, x, y);
}

void citron_ios_touch_ended(int id) {
    IOS::EmulationSession::GetInstance().TouchReleased(id);
}

void citron_ios_set_button(int player_index, int button_id, bool pressed) {
    IOS::EmulationSession::GetInstance().SetButtonState(static_cast<std::size_t>(player_index),
                                                        button_id, pressed);
}

void citron_ios_set_stick(int player_index, int stick_id, float x, float y) {
    IOS::EmulationSession::GetInstance().SetStickPosition(static_cast<std::size_t>(player_index),
                                                          stick_id, x, y);
}

static bool CanMakeExecutableMemory(int extra_mmap_flags) {
    constexpr std::size_t test_size = 16 * 1024 * 1024;
    void* memory = mmap(nullptr, test_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON | extra_mmap_flags, -1, 0);
    if (memory == MAP_FAILED) {
        return false;
    }
    const bool can_execute = mprotect(memory, test_size, PROT_READ | PROT_EXEC) == 0 &&
                             mprotect(memory, test_size, PROT_READ | PROT_WRITE) == 0 &&
                             mprotect(memory, test_size, PROT_READ | PROT_EXEC) == 0;
    munmap(memory, test_size);
    return can_execute;
}

// Dynarmic emits and toggles JIT pages from guest CPU threads (fibers), not the main thread.
// iOS may allow mprotect from the UI thread while still rejecting the same sequence elsewhere.
static bool CanMakeExecutableMemoryOnWorkerThread(int extra_mmap_flags) {
    bool ok = false;
    std::thread t([&] { ok = CanMakeExecutableMemory(extra_mmap_flags); });
    t.join();
    return ok;
}

bool citron_ios_jit_available() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    for (int attempt = 0; attempt < 5; ++attempt) {
        CitronIosResolveJitWriteProtectPointer();
        if (citron_ios_pthread_jit_write_protect_np != nullptr ||
            citron_ios_pthread_jit_write_with_callback_np != nullptr) {
            break;
        }
        if (attempt + 1 < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
#    if !defined(HAS_NCE)
    if (citron_ios_pthread_jit_write_protect_np == nullptr &&
        citron_ios_pthread_jit_write_with_callback_np == nullptr) {
        return false;
    }
#    endif
    return true;
#else
#if defined(MAP_JIT)
    if (CanMakeExecutableMemoryOnWorkerThread(MAP_JIT)) {
        return true;
    }
#endif
    return CanMakeExecutableMemoryOnWorkerThread(0);
#endif
}

}
