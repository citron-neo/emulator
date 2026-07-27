// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <string>
#include <string_view>
#include <dlfcn.h>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>

#ifdef ARCHITECTURE_arm64
#include <adrenotools/driver.h>
#endif

#include <android/api-level.h>
#include <android/native_window_jni.h>
#include <common/fs/file.h>
#include <common/fs/fs.h>
#include <core/file_sys/patch_manager.h>
#include <core/file_sys/savedata_factory.h>
#include <core/loader/nro.h>
#include <frontend_common/content_manager.h>
#include <jni.h>

#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "common/detached_tasks.h"
#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/file_sys/romfs.h"
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
#include "core/internal_network/network_interface.h"
#include "core/loader/loader.h"
#include "core/memory/cheat_engine.h"
#include "frontend_common/config.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "jni/native.h"
#include "network/network.h"
#include "network/room_member.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/shader_notify.h"

// [UNITY-FIX] winbase.h A/W macros shadow C++ method names.
#undef DeleteFile
#undef CreateFile
#undef CopyFile
#undef MoveFile
#undef MoveFileEx
#undef CreateDirectory
#undef RemoveDirectory

#define jconst [[maybe_unused]] const auto
#define jauto [[maybe_unused]] auto

namespace {

std::function<bool(size_t, size_t)> CreateLongProgressCallback(JNIEnv* env, jobject jcallback) {
    if (jcallback == nullptr) {
        return [](size_t, size_t) { return true; };
    }

    const auto jlambda_class = env->GetObjectClass(jcallback);
    const auto jlambda_invoke_method = env->GetMethodID(
        jlambda_class, "invoke", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    env->DeleteLocalRef(jlambda_class);

    const auto jlong_class = env->FindClass("java/lang/Long");
    const auto jlong_value_of =
        env->GetStaticMethodID(jlong_class, "valueOf", "(J)Ljava/lang/Long;");

    return [env, jcallback, jlambda_invoke_method, jlong_class,
            jlong_value_of](size_t max, size_t progress) {
        const auto jmax =
            env->CallStaticObjectMethod(jlong_class, jlong_value_of, static_cast<jlong>(max));
        const auto jprogress = env->CallStaticObjectMethod(
            jlong_class, jlong_value_of, static_cast<jlong>(progress));

        const auto jwas_cancelled =
            env->CallObjectMethod(jcallback, jlambda_invoke_method, jmax, jprogress);

        if (jmax != nullptr) {
            env->DeleteLocalRef(jmax);
        }
        if (jprogress != nullptr) {
            env->DeleteLocalRef(jprogress);
        }

        if (env->ExceptionCheck()) {
            LOG_ERROR(Frontend, "Progress callback threw an exception; cancelling operation");
            env->ExceptionClear();
            return true;
        }

        if (jwas_cancelled == nullptr) {
            return false;
        }

        const bool was_cancelled = Common::Android::GetJBoolean(env, jwas_cancelled);
        env->DeleteLocalRef(jwas_cancelled);
        return was_cancelled;
    };
}

} // namespace

static EmulationSession s_instance;

EmulationSession::EmulationSession() {
    m_vfs = std::make_shared<FileSys::RealVfsFilesystem>();
    m_network_initialized = m_system.GetRoomNetwork().Init();
    if (!m_network_initialized) {
        LOG_ERROR(Network, "Failed to initialize Android room networking");
    }
}

EmulationSession::~EmulationSession() {
    SetNativeWindow(nullptr);
    if (m_network_initialized) {
        m_system.GetRoomNetwork().Shutdown();
    }
}

EmulationSession& EmulationSession::GetInstance() {
    return s_instance;
}

const Core::System& EmulationSession::System() const {
    return m_system;
}

Core::System& EmulationSession::System() {
    return m_system;
}

FileSys::ManualContentProvider* EmulationSession::GetContentProvider() {
    return m_manual_provider.get();
}

InputCommon::InputSubsystem& EmulationSession::GetInputSubsystem() {
    return m_input_subsystem;
}

const EmuWindow_Android& EmulationSession::Window() const {
    return *m_window;
}

EmuWindow_Android& EmulationSession::Window() {
    return *m_window;
}

void EmulationSession::SetNativeWindow(ANativeWindow* native_window) {
    std::scoped_lock lock(m_window_mutex);
    if (m_native_window == native_window) {
        // ANativeWindow_fromSurface acquires a reference even when it returns the same pointer.
        // Keep the reference already owned by the session and release the duplicate.
        if (native_window != nullptr) {
            ANativeWindow_release(native_window);
        }
        return;
    }
    if (m_native_window != nullptr) {
        ANativeWindow_release(m_native_window);
    }
    m_native_window = native_window;
}

void EmulationSession::InitializeGpuDriver(const std::string& hook_lib_dir,
                                           const std::string& custom_driver_dir,
                                           const std::string& custom_driver_name,
                                           const std::string& file_redirect_dir) {
#ifdef ARCHITECTURE_arm64
    void* handle{};
    const char* file_redirect_dir_{};
    int featureFlags{};

    // Enable driver file redirection when renderer debugging is enabled.
    if (Settings::values.renderer_debug && file_redirect_dir.size()) {
        featureFlags |= ADRENOTOOLS_DRIVER_FILE_REDIRECT;
        file_redirect_dir_ = file_redirect_dir.c_str();
    }

    // Try to load a custom driver.
    if (custom_driver_name.size()) {
        handle = adrenotools_open_libvulkan(
            RTLD_NOW, featureFlags | ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hook_lib_dir.c_str(),
            custom_driver_dir.c_str(), custom_driver_name.c_str(), file_redirect_dir_, nullptr);
    }

    // Try to load the system driver.
    if (!handle) {
        handle = adrenotools_open_libvulkan(RTLD_NOW, featureFlags, nullptr, hook_lib_dir.c_str(),
                                            nullptr, nullptr, file_redirect_dir_, nullptr);
    }

    m_vulkan_library = std::make_shared<Common::DynamicLibrary>(handle);
#endif
}

bool EmulationSession::IsRunning() const {
    return m_is_running;
}

bool EmulationSession::IsPaused() const {
    return m_is_running && m_is_paused;
}

bool EmulationSession::IsShuttingDown() const {
    return m_is_shutting_down;
}

bool EmulationSession::IsNetworkInitialized() const {
    return m_network_initialized;
}

std::unique_lock<std::mutex> EmulationSession::AcquireSessionLock() {
    return std::unique_lock{m_mutex};
}

const Core::PerfStatsResults& EmulationSession::PerfStats() {
    m_perf_stats = m_system.GetAndResetPerfStats();
    return m_perf_stats;
}

void EmulationSession::SurfaceChanged() {
    std::scoped_lock lock(m_window_mutex);
    if (!IsRunning() || m_window == nullptr || m_native_window == nullptr) {
        return;
    }
    m_window->OnSurfaceChanged(m_native_window);
}

void EmulationSession::ConfigureFilesystemProvider(const std::string& filepath) {
    const auto file = m_system.GetFilesystem()->OpenFile(filepath, FileSys::OpenMode::Read);
    if (!file) {
        return;
    }

    auto loader = Loader::GetLoader(m_system, file);
    if (!loader) {
        return;
    }

    const auto file_type = loader->GetFileType();
    if (file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) {
        return;
    }

    u64 program_id = 0;
    const auto res2 = loader->ReadProgramId(program_id);
    if (res2 == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
        m_manual_provider->AddEntry(FileSys::TitleType::Application,
                                    FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                    program_id, file);
    } else if (res2 == Loader::ResultStatus::Success &&
               (file_type == Loader::FileType::XCI || file_type == Loader::FileType::NSP)) {
        const auto nsp = file_type == Loader::FileType::NSP
                             ? std::make_shared<FileSys::NSP>(file)
                             : FileSys::XCI{file}.GetSecurePartitionNSP();
        for (const auto& title : nsp->GetNCAs()) {
            for (const auto& entry : title.second) {
                m_manual_provider->AddEntry(entry.first.first, entry.first.second, title.first,
                                            entry.second->GetBaseFile());
            }
        }
    }
}

void EmulationSession::InitializeSystem(bool reload) {
    std::scoped_lock lock(m_mutex);

    if (m_is_running || m_load_result == Core::SystemResultStatus::Success) {
        LOG_ERROR(Frontend, "Refusing to initialize system while emulation is active");
        return;
    }

    auto& fs_controller = m_system.GetFileSystemController();
    if (fs_controller.GetInitStage() < Service::FileSystem::InitStage::SETTINGS_READY) {
        LOG_ERROR(Frontend, "Refusing to initialize system before settings are ready");
        return;
    }

    if (!reload) {
        m_system.Initialize();

        // Initialize logging system
        Common::Log::Initialize();
        Common::Log::SetColorConsoleBackendEnabled(true);
        Common::Log::Start();

        m_input_subsystem.Initialize();
    }

    Core::Crypto::KeyManager::Instance().ReloadKeys();
    Core::Crypto::KeyManager::Instance().PopulateTickets();
    LOG_INFO(Frontend, "InitializeSystem: keys_loaded_before_content={}",
             Core::Crypto::KeyManager::Instance().AreKeysLoaded());

    // Initialize filesystem.
    m_system.SetFilesystem(m_vfs);
    m_system.GetUserChannel().clear();
    m_manual_provider = std::make_unique<FileSys::ManualContentProvider>();
    m_system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    m_system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual,
                                     m_manual_provider.get());
    fs_controller.SetInitStage(Service::FileSystem::InitStage::FS_READY);
    fs_controller.InitializeContentSystem(*m_vfs);
}


void EmulationSession::RefreshContentSystemUnlocked()
{
    auto& fs = m_system.GetFileSystemController();
    if (auto filesystem = m_system.GetFilesystem()) {
        fs.InitializeContentSystem(*filesystem);
    }
}


void EmulationSession::RefreshContentSystem() {
    std::scoped_lock lock(m_mutex);
    RefreshContentSystemUnlocked();
}


bool EmulationSession::RefreshContentIfIdle(bool keys_loaded)
{
    if (!keys_loaded) {
        return false;
    }

    std::scoped_lock lock(m_mutex);
    if (m_is_running || m_load_result == Core::SystemResultStatus::Success) {
        return false;
    }

    RefreshContentSystemUnlocked();
    return true;
}

void EmulationSession::SetFilesystemInitStage(Service::FileSystem::InitStage stage) {
    std::scoped_lock lock(m_mutex);
    m_system.GetFileSystemController().SetInitStage(stage);
}

void EmulationSession::PromoteFilesystemInitStage(Service::FileSystem::InitStage stage) {
    std::scoped_lock lock(m_mutex);
    auto& controller = m_system.GetFileSystemController();
    if (controller.GetInitStage() < stage) {
        controller.SetInitStage(stage);
    }
}

void EmulationSession::SetAppletId(int applet_id) {
    m_applet_id = applet_id;
    m_system.GetFrontendAppletHolder().SetCurrentAppletId(
        static_cast<Service::AM::AppletId>(m_applet_id));
}

Core::SystemResultStatus EmulationSession::InitializeEmulation(const std::string& filepath,
                                                               const std::size_t program_index,
                                                               const bool frontend_initiated) {
    std::unique_lock lock(m_mutex);
    m_session_cv.wait(lock, [this] { return !m_session_active; });
    m_session_active = true;

    // Create the render window.
    {
        std::scoped_lock window_lock(m_window_mutex);
        m_window = std::make_unique<EmuWindow_Android>(m_native_window, m_vulkan_library);
    }

    // Initialize system.
    jauto android_keyboard = std::make_unique<Common::Android::SoftwareKeyboard::AndroidKeyboard>();
    m_software_keyboard = android_keyboard.get();
    m_system.SetShuttingDown(false);
    Settings::values.swkbd_applet_mode.SetValue(Settings::AppletMode::HLE);
    m_system.ApplySettings();
    Settings::LogSettings();
    m_system.HIDCore().ReloadInputDevices();
    m_system.SetFrontendAppletSet({
        nullptr,                     // Amiibo Settings
        nullptr,                     // Controller Selector
        nullptr,                     // Error Display
        nullptr,                     // Mii Editor
        nullptr,                     // Parental Controls
        nullptr,                     // Photo Viewer
        nullptr,                     // Profile Selector
        std::move(android_keyboard), // Software Keyboard
        nullptr,                     // Web Browser
    });

    // Initialize filesystem.
    ConfigureFilesystemProvider(filepath);

    // Load the ROM.
    Service::AM::FrontendAppletParameters params{
        .applet_id = static_cast<Service::AM::AppletId>(m_applet_id),
        .launch_type = frontend_initiated ? Service::AM::LaunchType::FrontendInitiated
                                          : Service::AM::LaunchType::ApplicationInitiated,
        .program_index = static_cast<s32>(program_index),
    };
    m_load_result = m_system.Load(EmulationSession::GetInstance().Window(), filepath, params);
    if (m_load_result != Core::SystemResultStatus::Success) {
        return m_load_result;
    }

    // Complete initialization.
    m_system.GPU().Start();
    m_system.GetCpuManager().OnGpuReady();
    m_system.RegisterExitCallback([&] { HaltEmulation(); });

    // Register an ExecuteProgram callback such that Core can execute a sub-program
    m_system.RegisterExecuteProgramCallback([&](std::size_t program_index_) {
        m_next_program_index = program_index_;
        EmulationSession::GetInstance().HaltEmulation();
    });

    OnEmulationStarted();
    return Core::SystemResultStatus::Success;
}

void EmulationSession::ShutdownEmulation() {
    m_is_shutting_down = true;
    std::scoped_lock lock(m_mutex);
    SCOPE_EXIT {
        m_session_active = false;
        m_is_shutting_down = false;
        m_session_cv.notify_all();
    };

    if (m_next_program_index != -1) {
        ChangeProgram(m_next_program_index);
        m_next_program_index = -1;
    }

    m_is_running = false;
    m_is_paused = false;

    // Unload user input.
    m_system.HIDCore().UnloadInputDevices();

    // Enable all controllers
    m_system.HIDCore().SetSupportedStyleTag({Core::HID::NpadStyleSet::All});

    // Shutdown the main emulated process
    if (m_load_result == Core::SystemResultStatus::Success) {
        m_system.DetachDebugger();
        m_system.ShutdownMainProcess();
        m_detached_tasks.WaitForAllTasks();
        m_load_result = Core::SystemResultStatus::ErrorNotInitialized;
        {
            std::scoped_lock window_lock(m_window_mutex);
            m_window.reset();
        }
        OnEmulationStopped(Core::SystemResultStatus::Success);
        return;
    }

    // Tear down the render window.
    {
        std::scoped_lock window_lock(m_window_mutex);
        m_window.reset();
    }
}

void EmulationSession::PauseEmulation() {
    std::scoped_lock lock(m_mutex);
    m_system.Pause();
    m_is_paused = true;
}

void EmulationSession::UnPauseEmulation() {
    std::scoped_lock lock(m_mutex);
    m_system.Run();
    m_is_paused = false;
}

void EmulationSession::HaltEmulation() {
    std::scoped_lock lock(m_mutex);
    m_is_shutting_down = true;
    m_is_running = false;
    m_cv.notify_one();
}

void EmulationSession::RunEmulation() {
    {
        std::scoped_lock lock(m_mutex);
        m_is_paused = false;
        m_is_running = true;
    }

    // Load the disk shader cache.
    if (Settings::values.use_disk_shader_cache.GetValue()) {
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
        m_system.Renderer().ReadRasterizer()->LoadDiskResources(
            m_system.GetApplicationProcessProgramID(), std::stop_token{}, LoadDiskCacheProgress);
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Complete, 0, 0);
    }

    void(m_system.Run());

    if (m_system.DebuggerEnabled()) {
        m_system.InitializeDebugger();
    }

    while (true) {
        {
            [[maybe_unused]] std::unique_lock lock(m_mutex);
            if (m_cv.wait_for(lock, std::chrono::milliseconds(800),
                              [&]() { return !m_is_running; })) {
                // Emulation halted.
                break;
            }
        }
    }

    // Reset current applet ID.
    m_applet_id = static_cast<int>(Service::AM::AppletId::Application);
}

Common::Android::SoftwareKeyboard::AndroidKeyboard* EmulationSession::SoftwareKeyboard() {
    return m_software_keyboard;
}

void EmulationSession::LoadDiskCacheProgress(VideoCore::LoadCallbackStage stage, int progress,
                                             int max) {
    JNIEnv* env = Common::Android::GetEnvForThread();
    env->CallStaticVoidMethod(Common::Android::GetDiskCacheProgressClass(),
                              Common::Android::GetDiskCacheLoadProgress(), static_cast<jint>(stage),
                              static_cast<jint>(progress), static_cast<jint>(max));
}

void EmulationSession::OnEmulationStarted() {
    JNIEnv* env = Common::Android::GetEnvForThread();
    env->CallStaticVoidMethod(Common::Android::GetNativeLibraryClass(),
                              Common::Android::GetOnEmulationStarted());
}

void EmulationSession::OnEmulationStopped(Core::SystemResultStatus result) {
    JNIEnv* env = Common::Android::GetEnvForThread();
    env->CallStaticVoidMethod(Common::Android::GetNativeLibraryClass(),
                              Common::Android::GetOnEmulationStopped(), static_cast<jint>(result));
}

void EmulationSession::ChangeProgram(std::size_t program_index) {
    JNIEnv* env = Common::Android::GetEnvForThread();
    env->CallStaticVoidMethod(Common::Android::GetNativeLibraryClass(),
                              Common::Android::GetOnProgramChanged(),
                              static_cast<jint>(program_index));
}

u64 EmulationSession::GetProgramId(JNIEnv* env, jstring jprogramId) {
    auto program_id_string = Common::Android::GetJString(env, jprogramId);
    try {
        std::size_t parsed_length = 0;
        const auto program_id = std::stoull(program_id_string, &parsed_length, 10);
        if (parsed_length != program_id_string.size()) {
            LOG_ERROR(Frontend, "Invalid decimal program ID '{}'", program_id_string);
            return 0;
        }
        return program_id;
    } catch (...) {
        LOG_ERROR(Frontend, "Failed to parse program ID '{}'", program_id_string);
        return 0;
    }
}

static Core::SystemResultStatus RunEmulation(const std::string& filepath, const size_t program_index, const bool frontend_initiated) {
    LOG_INFO(Frontend, "starting");

    if (filepath.empty()) {
        LOG_CRITICAL(Frontend, "failed to load: filepath empty!");
        return Core::SystemResultStatus::ErrorLoader;
    }

    SCOPE_EXIT {
        EmulationSession::GetInstance().ShutdownEmulation();
    };

    jconst result = EmulationSession::GetInstance().InitializeEmulation(filepath, program_index,
                                                                        frontend_initiated);
    if (result != Core::SystemResultStatus::Success) {
        return result;
    }

    EmulationSession::GetInstance().RunEmulation();

    return Core::SystemResultStatus::Success;
}

extern "C" {

void Java_org_citron_citron_1emu_NativeLibrary_surfaceChanged(JNIEnv* env, jobject instance,
                                                          [[maybe_unused]] jobject surf) {
    EmulationSession::GetInstance().SetNativeWindow(ANativeWindow_fromSurface(env, surf));
    EmulationSession::GetInstance().SurfaceChanged();
}

void Java_org_citron_citron_1emu_NativeLibrary_surfaceDestroyed(JNIEnv* env, jobject instance) {
    EmulationSession::GetInstance().SetNativeWindow(nullptr);
    EmulationSession::GetInstance().SurfaceChanged();
}

void Java_org_citron_citron_1emu_NativeLibrary_setAppDirectory(JNIEnv* env, jobject instance,
                                                           [[maybe_unused]] jstring j_directory) {
    Common::FS::SetAppDirectory(Common::Android::GetJString(env, j_directory));
}

int Java_org_citron_citron_1emu_NativeLibrary_installFileToNand(JNIEnv* env, jobject instance,
                                                              jstring j_file, jobject jcallback) {
    auto jlambdaClass = env->GetObjectClass(jcallback);
    auto jlambdaInvokeMethod = env->GetMethodID(
        jlambdaClass, "invoke", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    const auto callback = [env, jcallback, jlambdaInvokeMethod](size_t max, size_t progress) {
        auto jwasCancelled = env->CallObjectMethod(jcallback, jlambdaInvokeMethod,
                                                   Common::Android::ToJDouble(env, max),
                                                   Common::Android::ToJDouble(env, progress));
        return Common::Android::GetJBoolean(env, jwasCancelled);
    };

    return static_cast<int>(
        ContentManager::InstallNSP(EmulationSession::GetInstance().System(),
                                   *EmulationSession::GetInstance().System().GetFilesystem(),
                                   Common::Android::GetJString(env, j_file), callback));
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_doesUpdateMatchProgram(JNIEnv* env, jobject jobj,
                                                                      jstring jprogramId,
                                                                      jstring jupdatePath) {
    u64 program_id = EmulationSession::GetProgramId(env, jprogramId);
    std::string updatePath = Common::Android::GetJString(env, jupdatePath);
    std::shared_ptr<FileSys::NSP> nsp = std::make_shared<FileSys::NSP>(
        EmulationSession::GetInstance().System().GetFilesystem()->OpenFile(
            updatePath, FileSys::OpenMode::Read));
    for (const auto& item : nsp->GetNCAs()) {
        for (const auto& nca_details : item.second) {
            if (nca_details.second->GetName().ends_with(".cnmt.nca")) {
                auto update_id = nca_details.second->GetTitleId() & ~0xFFFULL;
                if (update_id == program_id) {
                    return true;
                }
            }
        }
    }
    return false;
}

void JNICALL Java_org_citron_citron_1emu_NativeLibrary_initializeGpuDriver(JNIEnv* env, jclass clazz,
                                                                       jstring hook_lib_dir,
                                                                       jstring custom_driver_dir,
                                                                       jstring custom_driver_name,
                                                                       jstring file_redirect_dir) {
    EmulationSession::GetInstance().InitializeGpuDriver(
        Common::Android::GetJString(env, hook_lib_dir),
        Common::Android::GetJString(env, custom_driver_dir),
        Common::Android::GetJString(env, custom_driver_name),
        Common::Android::GetJString(env, file_redirect_dir));
}

[[maybe_unused]] static bool CheckKgslPresent() {
    constexpr auto KgslPath{"/dev/kgsl-3d0"};

    return access(KgslPath, F_OK) == 0;
}

[[maybe_unused]] bool SupportsCustomDriver() {
    return android_get_device_api_level() >= 28 && CheckKgslPresent();
}

jboolean JNICALL Java_org_citron_citron_1emu_utils_GpuDriverHelper_supportsCustomDriverLoading(
    JNIEnv* env, jobject instance) {
#ifdef ARCHITECTURE_arm64
    // If the KGSL device exists custom drivers can be loaded using adrenotools
    return SupportsCustomDriver();
#else
    return false;
#endif
}

jobjectArray Java_org_citron_citron_1emu_utils_GpuDriverHelper_getSystemDriverInfoNative(
    JNIEnv* env, jobject j_obj, jobject j_surf, jstring j_hook_lib_dir) {
    const char* file_redirect_dir_{};
    int featureFlags{};
    std::string hook_lib_dir = Common::Android::GetJString(env, j_hook_lib_dir);
    auto handle = adrenotools_open_libvulkan(RTLD_NOW, featureFlags, nullptr, hook_lib_dir.c_str(),
                                             nullptr, nullptr, file_redirect_dir_, nullptr);
    auto driver_library = std::make_shared<Common::DynamicLibrary>(handle);
    ANativeWindow* const native_window = ANativeWindow_fromSurface(env, j_surf);
    SCOPE_EXIT {
        if (native_window != nullptr) {
            ANativeWindow_release(native_window);
        }
    };
    auto window = std::make_unique<EmuWindow_Android>(native_window, driver_library);

    Vulkan::vk::InstanceDispatch dld;
    Vulkan::vk::Instance vk_instance = Vulkan::CreateInstance(
        *driver_library, dld, VK_API_VERSION_1_1, Core::Frontend::WindowSystemType::Android);

    auto surface = Vulkan::CreateSurface(vk_instance, window->GetWindowInfo());

    auto device = Vulkan::CreateDevice(vk_instance, dld, *surface);

    auto driver_version = device.GetDriverVersion();
    auto version_string =
        fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(driver_version),
                    VK_API_VERSION_MINOR(driver_version), VK_API_VERSION_PATCH(driver_version));

    jobjectArray j_driver_info = env->NewObjectArray(
        2, Common::Android::GetStringClass(), Common::Android::ToJString(env, version_string));
    env->SetObjectArrayElement(j_driver_info, 1,
                               Common::Android::ToJString(env, device.GetDriverName()));
    return j_driver_info;
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_reloadKeys(JNIEnv* env, jclass clazz) {
    auto& session = EmulationSession::GetInstance();
    Core::Crypto::KeyManager::Instance().ReloadKeys();
    const bool keys_loaded = Core::Crypto::KeyManager::Instance().AreKeysLoaded();

    const bool refreshed_content = session.RefreshContentIfIdle(keys_loaded);
    LOG_INFO(Frontend,
         "reloadKeys: keys_loaded={}, refreshed_content={}",
         keys_loaded, refreshed_content);

    return static_cast<jboolean>(keys_loaded);
}

void Java_org_citron_citron_1emu_NativeLibrary_unpauseEmulation(JNIEnv* env, jclass clazz) {
    EmulationSession::GetInstance().UnPauseEmulation();
}

void Java_org_citron_citron_1emu_NativeLibrary_pauseEmulation(JNIEnv* env, jclass clazz) {
    EmulationSession::GetInstance().PauseEmulation();
}

void Java_org_citron_citron_1emu_NativeLibrary_stopEmulation(JNIEnv* env, jclass clazz) {
    EmulationSession::GetInstance().HaltEmulation();
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_isRunning(JNIEnv* env, jclass clazz) {
    const auto& session = EmulationSession::GetInstance();
    return static_cast<jboolean>(session.IsRunning() || session.IsShuttingDown());
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_isPaused(JNIEnv* env, jclass clazz) {
    return static_cast<jboolean>(EmulationSession::GetInstance().IsPaused());
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_connectToRoom(JNIEnv* env, jobject jobj,
                                                               jstring jnickname, jstring jhost,
                                                               jint jport) {
    auto& session = EmulationSession::GetInstance();
    if (!session.IsNetworkInitialized() || jport <= 0 || jport > std::numeric_limits<u16>::max()) {
        return false;
    }

    const auto nickname = Common::Android::GetJString(env, jnickname);
    const auto host = Common::Android::GetJString(env, jhost);
    if (nickname.empty() || nickname.size() > 20 || host.empty() || host.size() > 253) {
        return false;
    }

    if (!Network::GetSelectedNetworkInterface()) {
        Network::SelectFirstNetworkInterface();
    }
    if (!Network::GetSelectedNetworkInterface()) {
        return false;
    }

    const auto room_member = session.System().GetRoomNetwork().GetRoomMember().lock();
    if (!room_member || room_member->GetState() == Network::RoomMember::State::Joining ||
        room_member->IsConnected()) {
        return false;
    }

    room_member->Join(nickname, host.c_str(), static_cast<u16>(jport));
    return room_member->IsConnected();
}

jint Java_org_citron_citron_1emu_NativeLibrary_getRoomConnectionState(JNIEnv* env, jobject jobj) {
    const auto room_member =
        EmulationSession::GetInstance().System().GetRoomNetwork().GetRoomMember().lock();
    if (!room_member) {
        return static_cast<jint>(Network::RoomMember::State::Uninitialized);
    }
    return static_cast<jint>(room_member->GetState());
}

void Java_org_citron_citron_1emu_NativeLibrary_leaveRoom(JNIEnv* env, jobject jobj) {
    const auto room_member =
        EmulationSession::GetInstance().System().GetRoomNetwork().GetRoomMember().lock();
    if (room_member && room_member->IsConnected()) {
        room_member->Leave();
    }
}

void Java_org_citron_citron_1emu_NativeLibrary_initializeSystem(JNIEnv* env, jclass clazz,
                                                            jboolean reload) {
    // Initialize the emulated system.
    EmulationSession::GetInstance().InitializeSystem(reload);
}

jdoubleArray Java_org_citron_citron_1emu_NativeLibrary_getPerfStats(JNIEnv* env, jclass clazz) {
    jdoubleArray j_stats = env->NewDoubleArray(4);

    if (EmulationSession::GetInstance().IsRunning()) {
        jconst results = EmulationSession::GetInstance().PerfStats();

        // Converting the structure into an array makes it easier to pass it to the frontend
        double stats[4] = {results.system_fps, results.average_game_fps, results.frametime,
                           results.emulation_speed};

        env->SetDoubleArrayRegion(j_stats, 0, 4, stats);
    }

    return j_stats;
}

jint Java_org_citron_citron_1emu_NativeLibrary_getShadersBuilding(JNIEnv* env, jclass clazz) {
    if (EmulationSession::GetInstance().IsRunning()) {
        return EmulationSession::GetInstance().System().GPU().ShaderNotify().ShadersBuilding();
    }
    return 0;
}

jstring Java_org_citron_citron_1emu_NativeLibrary_getCpuBackend(JNIEnv* env, jclass clazz) {
    if (Settings::IsNceEnabled()) {
        return Common::Android::ToJString(env, "NCE");
    }

    return Common::Android::ToJString(env, "JIT");
}

jstring Java_org_citron_citron_1emu_NativeLibrary_getGpuDriver(JNIEnv* env, jobject jobj) {
    return Common::Android::ToJString(
        env, EmulationSession::GetInstance().System().GPU().Renderer().GetDeviceVendor());
}

void Java_org_citron_citron_1emu_NativeLibrary_applySettings(JNIEnv* env, jobject jobj) {
    EmulationSession::GetInstance().System().ApplySettings();
    EmulationSession::GetInstance().System().HIDCore().ReloadInputDevices();
}

void Java_org_citron_citron_1emu_NativeLibrary_logSettings(JNIEnv* env, jobject jobj) {
    Settings::LogSettings();
}

void Java_org_citron_citron_1emu_NativeLibrary_run(JNIEnv* env, jobject jobj, jstring j_path,
                                               jint j_program_index,
                                               jboolean j_frontend_initiated) {
    const std::string path = Common::Android::GetJString(env, j_path);

    const Core::SystemResultStatus result{
        RunEmulation(path, j_program_index, j_frontend_initiated)};
    if (result != Core::SystemResultStatus::Success) {
        env->CallStaticVoidMethod(Common::Android::GetNativeLibraryClass(),
                                  Common::Android::GetExitEmulationActivity(),
                                  static_cast<int>(result));
    }
}

void Java_org_citron_citron_1emu_NativeLibrary_logDeviceInfo(JNIEnv* env, jclass clazz) {
    LOG_INFO(Frontend, "citron Version: {}-{}", Common::g_scm_branch, Common::g_scm_desc);
    LOG_INFO(Frontend, "Host OS: Android API level {}", android_get_device_api_level());
}

void Java_org_citron_citron_1emu_NativeLibrary_submitInlineKeyboardText(JNIEnv* env, jclass clazz,
                                                                    jstring j_text) {
    const std::u16string input = Common::UTF8ToUTF16(Common::Android::GetJString(env, j_text));
    EmulationSession::GetInstance().SoftwareKeyboard()->SubmitInlineKeyboardText(input);
}

void Java_org_citron_citron_1emu_NativeLibrary_replaceInlineKeyboardText(JNIEnv* env, jclass clazz,
                                                                         jstring j_text,
                                                                         jint j_cursor_position) {
    const std::u16string input = Common::UTF8ToUTF16(Common::Android::GetJString(env, j_text));
    EmulationSession::GetInstance().SoftwareKeyboard()->ReplaceInlineKeyboardText(
        input, static_cast<s32>(j_cursor_position));
}

void Java_org_citron_citron_1emu_NativeLibrary_submitInlineKeyboardInput(JNIEnv* env, jclass clazz,
                                                                     jint j_key_code) {
    EmulationSession::GetInstance().SoftwareKeyboard()->SubmitInlineKeyboardInput(j_key_code);
}

void Java_org_citron_citron_1emu_NativeLibrary_initializeEmptyUserDirectory(JNIEnv* env,
                                                                        jobject instance) {
    const auto nand_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir);
    auto vfs_nand_dir = EmulationSession::GetInstance().System().GetFilesystem()->OpenDirectory(
        Common::FS::PathToUTF8String(nand_dir), FileSys::OpenMode::Read);

    const auto user_id = EmulationSession::GetInstance().System().GetProfileManager().GetUser(
        static_cast<std::size_t>(0));
    ASSERT(user_id);

    const auto user_save_data_path = FileSys::SaveDataFactory::GetFullPath(
        {}, vfs_nand_dir, FileSys::SaveDataSpaceId::User, FileSys::SaveDataType::Account, 1,
        user_id->AsU128(), 0);

    const auto full_path = Common::FS::ConcatPathSafe(nand_dir, user_save_data_path);
    if (!Common::FS::CreateParentDirs(full_path)) {
        LOG_WARNING(Frontend, "Failed to create full path of the default user's save directory");
    }
}

jstring Java_org_citron_citron_1emu_NativeLibrary_getAppletLaunchPath(JNIEnv* env, jclass clazz,
                                                                  jlong jid) {
    auto bis_system =
        EmulationSession::GetInstance().System().GetFileSystemController().GetSystemNANDContents();
    if (!bis_system) {
        return Common::Android::ToJString(env, "");
    }

    auto applet_nca =
        bis_system->GetEntry(static_cast<u64>(jid), FileSys::ContentRecordType::Program);
    if (!applet_nca) {
        return Common::Android::ToJString(env, "");
    }

    return Common::Android::ToJString(env, applet_nca->GetFullPath());
}

void Java_org_citron_citron_1emu_NativeLibrary_setCurrentAppletId(JNIEnv* env, jclass clazz,
                                                              jint jappletId) {
    EmulationSession::GetInstance().SetAppletId(jappletId);
}

void Java_org_citron_citron_1emu_NativeLibrary_setCabinetMode(JNIEnv* env, jclass clazz,
                                                          jint jcabinetMode) {
    EmulationSession::GetInstance().System().GetFrontendAppletHolder().SetCabinetMode(
        static_cast<Service::NFP::CabinetMode>(jcabinetMode));
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_isFirmwareAvailable(JNIEnv* env, jclass clazz) {
    auto bis_system =
        EmulationSession::GetInstance().System().GetFileSystemController().GetSystemNANDContents();
    if (!bis_system) {
        return false;
    }

    // refresh to fix firmware detection on app restart
    bis_system->Refresh();

    // Query an applet to see if it's available
    auto applet_nca =
        bis_system->GetEntry(0x010000000000100Dull, FileSys::ContentRecordType::Program);
    if (!applet_nca) {
        return false;
    }
    return true;
}

jobjectArray Java_org_citron_citron_1emu_NativeLibrary_getPatchesForFile(JNIEnv* env, jobject jobj,
                                                                     jstring jpath,
                                                                     jstring jprogramId) {
    const auto path = Common::Android::GetJString(env, jpath);
    const auto vFile =
        Core::GetGameFileFromPath(EmulationSession::GetInstance().System().GetFilesystem(), path);
    if (vFile == nullptr) {
        return nullptr;
    }

    auto& system = EmulationSession::GetInstance().System();
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto loader = Loader::GetLoader(system, vFile);

    FileSys::VirtualFile update_raw;
    loader->ReadUpdateRaw(update_raw);

    auto patches = pm.GetPatches(update_raw);
    jobjectArray jpatchArray =
        env->NewObjectArray(patches.size(), Common::Android::GetPatchClass(), nullptr);
    int i = 0;
    for (const auto& patch : patches) {
        jobject jpatch = env->NewObject(
            Common::Android::GetPatchClass(), Common::Android::GetPatchConstructor(), patch.enabled,
            Common::Android::ToJString(env, patch.name),
            Common::Android::ToJString(env, patch.version), static_cast<jint>(patch.type),
            Common::Android::ToJString(env, std::to_string(patch.program_id)),
            Common::Android::ToJString(env, std::to_string(patch.title_id)), patch.removable);
        env->SetObjectArrayElement(jpatchArray, i, jpatch);
        ++i;
    }
    return jpatchArray;
}

jobjectArray Java_org_citron_citron_1emu_NativeLibrary_getCheatsForFile(JNIEnv* env, jobject jobj,
                                                                    jstring jpath,
                                                                    jstring jprogramId) {
    (void)jpath;
    auto& system = EmulationSession::GetInstance().System();
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto* cheat_engine = system.GetCheatEngine();
    if (cheat_engine == nullptr) {
        return env->NewObjectArray(0, Common::Android::GetPatchClass(), nullptr);
    }
    const auto active_build_id = FileSys::GetCheatBuildId(cheat_engine->GetBuildId());

    const auto cheats = pm.GetCheats();
    std::vector<FileSys::CheatPatch> active_cheats;
    active_cheats.reserve(cheats.size());
    std::copy_if(cheats.begin(), cheats.end(), std::back_inserter(active_cheats),
                 [&active_build_id](const FileSys::CheatPatch& cheat) {
                     return cheat.build_id == active_build_id;
                 });

    jobjectArray jpatchArray =
        env->NewObjectArray(active_cheats.size(), Common::Android::GetPatchClass(), nullptr);
    int i = 0;
    for (const auto& cheat : active_cheats) {
        const auto jname = Common::Android::ToJString(env, cheat.name);
        const auto jversion = Common::Android::ToJString(env, cheat.source);
        const auto jpatchProgramId = Common::Android::ToJString(env, std::to_string(program_id));
        const auto jbuildId = Common::Android::ToJString(env, cheat.build_id);
        jobject jpatch = env->NewObject(
            Common::Android::GetPatchClass(), Common::Android::GetPatchConstructor(), cheat.enabled,
            jname, jversion, static_cast<jint>(FileSys::PatchType::Cheat), jpatchProgramId,
            jbuildId, false);
        env->SetObjectArrayElement(jpatchArray, i, jpatch);
        env->DeleteLocalRef(jpatch);
        env->DeleteLocalRef(jname);
        env->DeleteLocalRef(jversion);
        env->DeleteLocalRef(jpatchProgramId);
        env->DeleteLocalRef(jbuildId);
        ++i;
    }
    return jpatchArray;
}

void Java_org_citron_citron_1emu_NativeLibrary_setCheatEnabled(JNIEnv* env, jobject jobj,
                                                           jstring jbuildId, jstring jsource,
                                                           jstring jname, jboolean jenabled) {
    const auto build_id =
        FileSys::NormalizeCheatBuildId(Common::Android::GetJString(env, jbuildId));
    const auto source = Common::Android::GetJString(env, jsource);
    const auto name = Common::Android::GetJString(env, jname);

    if (build_id.empty() || source.empty() || name.empty()) {
        return;
    }

    auto& disabled_cheats = Settings::values.disabled_cheats[build_id];
    const auto cheat_key = FileSys::GetCheatConfigKey(source, name);
    if (jenabled) {
        disabled_cheats.erase(cheat_key);
        // Migrate the old build-ID/name-only state when the user changes this cheat.
        disabled_cheats.erase(name);
        if (disabled_cheats.empty()) {
            Settings::values.disabled_cheats.erase(build_id);
        }
    } else {
        disabled_cheats.insert(cheat_key);
    }
}

void Java_org_citron_citron_1emu_NativeLibrary_disableCheatsForAddon(JNIEnv* env, jobject jobj,
                                                                  jstring jprogramId,
                                                                  jstring jaddonName) {
    auto& system = EmulationSession::GetInstance().System();
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    const auto addon_name = Common::Android::GetJString(env, jaddonName);
    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};

    for (const auto& cheat : pm.GetCheatsForMod(addon_name)) {
        Settings::values.disabled_cheats[cheat.build_id].insert(
            FileSys::GetCheatConfigKey(cheat.source, cheat.name));
    }
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_reloadCheats(JNIEnv* env, jobject jobj,
                                                            jstring jprogramId) {
    auto& system = EmulationSession::GetInstance().System();
    auto* cheat_engine = system.GetCheatEngine();
    if (cheat_engine == nullptr) {
        return false;
    }

    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    cheat_engine->Reload(pm.CreateCheatList(cheat_engine->GetBuildId()));
    return true;
}

jint Java_org_citron_citron_1emu_NativeLibrary_addCheat(JNIEnv* env, jobject jobj,
                                                     jstring jprogramId, jstring jtitle,
                                                     jstring jcode) {
    enum class Result : jint {
        Success = 0,
        InvalidTitle = 1,
        InvalidCode = 2,
        NoCheatEngine = 3,
        UnableToWrite = 4,
        DuplicateTitle = 5,
    };

    constexpr std::size_t MaxCheatFileSize = 1024 * 1024;
    constexpr std::string_view HotCheatsDirectory = "Hot Cheats";

    auto& session = EmulationSession::GetInstance();
    const auto session_lock = session.AcquireSessionLock();
    auto& system = session.System();
    auto* cheat_engine = system.GetCheatEngine();
    if (cheat_engine == nullptr) {
        return static_cast<jint>(Result::NoCheatEngine);
    }

    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    const auto title = Common::StripSpaces(Common::Android::GetJString(env, jtitle));
    const auto code = Common::StripSpaces(Common::Android::GetJString(env, jcode));
    if (program_id == 0 || title.empty() ||
        title.size() >= Core::Memory::CheatDefinition{}.readable_name.size() ||
        title.find_first_of("[]{}\r\n") != std::string::npos) {
        return static_cast<jint>(Result::InvalidTitle);
    }

    const auto entry_text = fmt::format("[{}]\n{}\n", title, code);
    const Core::Memory::TextCheatParser parser;
    const auto new_entries = parser.Parse(entry_text);
    if (new_entries.size() != 2 || new_entries[1].definition.num_opcodes == 0) {
        return static_cast<jint>(Result::InvalidCode);
    }

    const auto build_id = FileSys::GetCheatBuildId(cheat_engine->GetBuildId());
    const auto cheat_file =
        Common::FS::GetCitronPath(Common::FS::CitronPath::LoadDir) /
        fmt::format("{:016X}", program_id) / HotCheatsDirectory / "cheats" /
        fmt::format("{}.txt", build_id);
    static std::mutex add_cheat_mutex;
    const std::scoped_lock add_cheat_lock{add_cheat_mutex};
    if (!Common::FS::CreateParentDirs(cheat_file)) {
        return static_cast<jint>(Result::UnableToWrite);
    }

    std::string contents;
    if (Common::FS::Exists(cheat_file)) {
        const auto file_size = Common::FS::GetSize(cheat_file);
        if (file_size > MaxCheatFileSize) {
            return static_cast<jint>(Result::UnableToWrite);
        }
        contents =
            Common::FS::ReadStringFromFile(cheat_file, Common::FS::FileType::TextFile);
        if (contents.size() != file_size) {
            return static_cast<jint>(Result::UnableToWrite);
        }

        const auto current_entries = parser.Parse(contents);
        if (current_entries.empty()) {
            return static_cast<jint>(Result::UnableToWrite);
        }
        const auto has_duplicate = std::any_of(
            current_entries.cbegin(), current_entries.cend(), [&title](const auto& entry) {
                const auto& name = entry.definition.readable_name;
                const auto end = std::find(name.cbegin(), name.cend(), '\0');
                return std::string_view{name.data(),
                                        static_cast<std::size_t>(end - name.cbegin())} == title;
            });
        if (has_duplicate) {
            return static_cast<jint>(Result::DuplicateTitle);
        }
    }

    if (!contents.empty() && contents.back() != '\n') {
        contents.push_back('\n');
    }
    contents += entry_text;
    if (contents.size() > MaxCheatFileSize || parser.Parse(contents).empty()) {
        return static_cast<jint>(Result::UnableToWrite);
    }

    auto temporary_file = cheat_file;
    temporary_file += ".tmp";
    if (!Common::FS::RemoveFile(temporary_file)) {
        return static_cast<jint>(Result::UnableToWrite);
    }
    SCOPE_EXIT {
        void(Common::FS::RemoveFile(temporary_file));
    };
    if (Common::FS::WriteStringToFile(temporary_file, Common::FS::FileType::TextFile, contents) !=
        contents.size()) {
        return static_cast<jint>(Result::UnableToWrite);
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary_file, cheat_file, rename_error);
    if (rename_error) {
        return static_cast<jint>(Result::UnableToWrite);
    }

    auto& disabled_addons = Settings::values.disabled_addons[program_id];
    std::erase(disabled_addons, HotCheatsDirectory);
    auto& disabled_cheats = Settings::values.disabled_cheats[build_id];
    const auto source = fmt::format("{}/{}.txt", HotCheatsDirectory, build_id);
    disabled_cheats.erase(FileSys::GetCheatConfigKey(source, title));
    disabled_cheats.erase(title);
    if (disabled_cheats.empty()) {
        Settings::values.disabled_cheats.erase(build_id);
    }

    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    cheat_engine->Reload(pm.CreateCheatList(cheat_engine->GetBuildId()));
    return static_cast<jint>(Result::Success);
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_removeBaseContent(JNIEnv* env, jobject jobj,
                                                                     jstring jprogramId) {
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    return ContentManager::RemoveBaseContent(
        EmulationSession::GetInstance().System().GetFileSystemController(), program_id);
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_removeUpdate(JNIEnv* env, jobject jobj,
                                                                jstring jprogramId) {
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    return ContentManager::RemoveUpdate(
        EmulationSession::GetInstance().System().GetFileSystemController(), program_id);
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_hasInstalledUpdate(JNIEnv* env, jobject jobj,
                                                                      jstring jprogramId) {
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    return ContentManager::HasUpdate(
        EmulationSession::GetInstance().System().GetFileSystemController(), program_id);
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_hasInstalledDLC(JNIEnv* env, jobject jobj,
                                                                   jstring jprogramId) {
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    return ContentManager::HasDLC(
        EmulationSession::GetInstance().System().GetFileSystemController(), program_id);
}

void Java_org_citron_citron_1emu_NativeLibrary_removeDLC(JNIEnv* env, jobject jobj,
                                                     jstring jtitleId) {
    const auto title_id = EmulationSession::GetProgramId(env, jtitleId);
    ContentManager::RemoveDLC(
        EmulationSession::GetInstance().System().GetFileSystemController(), title_id);
}

jint Java_org_citron_citron_1emu_NativeLibrary_removeAllDLC(JNIEnv* env, jobject jobj,
                                                            jstring jprogramId) {
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    return static_cast<jint>(
        ContentManager::RemoveAllDLC(EmulationSession::GetInstance().System(), program_id));
}

void Java_org_citron_citron_1emu_NativeLibrary_removeMod(JNIEnv* env, jobject jobj, jstring jprogramId,
                                                     jstring jname) {
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    ContentManager::RemoveMod(EmulationSession::GetInstance().System().GetFileSystemController(),
                              program_id, Common::Android::GetJString(env, jname));
}

jobjectArray Java_org_citron_citron_1emu_NativeLibrary_verifyInstalledContents(JNIEnv* env,
                                                                            jobject jobj,
                                                                            jobject jcallback) {
    const auto callback = CreateLongProgressCallback(env, jcallback);

    auto& session = EmulationSession::GetInstance();
    auto* content_provider = session.GetContentProvider();
    if (content_provider == nullptr) {
        LOG_ERROR(Frontend, "Cannot verify installed contents before content provider is initialized");
        const auto message =
            Common::Android::ToJString(env, "Content provider is not initialized");
        return env->NewObjectArray(1, Common::Android::GetStringClass(), message);
    }

    std::vector<std::string> result = ContentManager::VerifyInstalledContents(
        session.System(), *content_provider, callback);
    jobjectArray jresult = env->NewObjectArray(result.size(), Common::Android::GetStringClass(),
                                               Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < result.size(); ++i) {
        env->SetObjectArrayElement(jresult, i, Common::Android::ToJString(env, result[i]));
    }
    return jresult;
}

jint Java_org_citron_citron_1emu_NativeLibrary_verifyGameContents(JNIEnv* env, jobject jobj,
                                                               jstring jpath, jobject jcallback) {
    const auto callback = CreateLongProgressCallback(env, jcallback);
    auto& session = EmulationSession::GetInstance();
    if (session.System().GetFilesystem() == nullptr) {
        LOG_ERROR(Frontend, "Cannot verify game contents before filesystem is initialized");
        return static_cast<jint>(ContentManager::GameVerificationResult::NotImplemented);
    }

    return static_cast<jint>(ContentManager::VerifyGameContents(
        session.System(), Common::Android::GetJString(env, jpath), callback));
}

jstring Java_org_citron_citron_1emu_NativeLibrary_getSavePath(JNIEnv* env, jobject jobj,
                                                          jstring jprogramId) {
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    if (program_id == 0) {
        return Common::Android::ToJString(env, "");
    }

    auto& system = EmulationSession::GetInstance().System();

    Service::Account::ProfileManager manager;
    // TODO: Pass in a selected user once we get the relevant UI working
    const auto user_id = manager.GetUser(static_cast<std::size_t>(0));
    ASSERT(user_id);

    const auto nandDir = Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir);
    auto vfsNandDir = system.GetFilesystem()->OpenDirectory(Common::FS::PathToUTF8String(nandDir),
                                                            FileSys::OpenMode::Read);

    const auto user_save_data_path = FileSys::SaveDataFactory::GetFullPath(
        {}, vfsNandDir, FileSys::SaveDataSpaceId::User, FileSys::SaveDataType::Account, program_id,
        user_id->AsU128(), 0);
    return Common::Android::ToJString(env, user_save_data_path);
}

jstring Java_org_citron_citron_1emu_NativeLibrary_getDefaultProfileSaveDataRoot(JNIEnv* env,
                                                                            jobject jobj,
                                                                            jboolean jfuture) {
    Service::Account::ProfileManager manager;
    // TODO: Pass in a selected user once we get the relevant UI working
    const auto user_id = manager.GetUser(static_cast<std::size_t>(0));
    ASSERT(user_id);

    const auto user_save_data_root =
        FileSys::SaveDataFactory::GetUserGameSaveDataRoot(user_id->AsU128(), jfuture);
    return Common::Android::ToJString(env, user_save_data_root);
}

void Java_org_citron_citron_1emu_NativeLibrary_addFileToFilesystemProvider(JNIEnv* env, jobject jobj,
                                                                       jstring jpath) {
    EmulationSession::GetInstance().ConfigureFilesystemProvider(
        Common::Android::GetJString(env, jpath));
}

void Java_org_citron_citron_1emu_NativeLibrary_clearFilesystemProvider(JNIEnv* env, jobject jobj) {
    EmulationSession::GetInstance().GetContentProvider()->ClearAllEntries();
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_areKeysPresent(JNIEnv* env, jobject jobj) {
    auto& session = EmulationSession::GetInstance();

    Core::Crypto::KeyManager::Instance().ReloadKeys();
    const bool keys_loaded =
        Core::Crypto::KeyManager::Instance().AreKeysLoaded();

    const bool refreshed_content = session.RefreshContentIfIdle(keys_loaded);

    LOG_INFO(Frontend, "areKeysPresent: refreshed_content={}", refreshed_content);

    return ContentManager::AreKeysPresent();
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_dumpRomFS(JNIEnv* env, jobject jobj,
                                                         jstring jgamePath, jstring jprogramId,
                                                         jstring jdumpPath, jobject jcallback) {
    (void)jdumpPath;
    // Check if emulation is running - dumping while emulation is active can cause crashes
    if (EmulationSession::GetInstance().IsRunning()) {
        LOG_ERROR(Frontend, "Cannot dump RomFS while emulation is running. Please close the game first.");
        return false;
    }

    const auto game_path = Common::Android::GetJString(env, jgamePath);
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    if (program_id == 0) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: invalid program ID");
        return false;
    }

    auto& system = EmulationSession::GetInstance().System();
    auto& vfs = *system.GetFilesystem();

    const auto game_file = vfs.OpenFile(game_path, FileSys::OpenMode::Read);
    if (game_file == nullptr) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: failed to open game file '{}'", game_path);
        return false;
    }
    const auto loader = Loader::GetLoader(system, game_file);
    if (loader == nullptr) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: no loader for '{}'", game_path);
        return false;
    }

    EmulationSession::GetInstance().ConfigureFilesystemProvider(game_path);

    FileSys::VirtualFile packed_update_raw{};
    loader->ReadUpdateRaw(packed_update_raw);

    const auto& installed = system.GetContentProvider();

    // Find the base NCA for the program
    u64 title_id = program_id;
    const auto type = FileSys::ContentRecordType::Program;
    auto base_nca = installed.GetEntry(title_id, type);
    if (!base_nca || base_nca->GetStatus() != Loader::ResultStatus::Success) {
        // Try to find any matching entry
        const auto entries = installed.ListEntriesFilter(FileSys::TitleType::Application, type);
        bool found = false;
        for (const auto& entry : entries) {
            if (FileSys::GetBaseTitleID(entry.title_id) == program_id) {
                title_id = entry.title_id;
                base_nca = installed.GetEntry(title_id, type);
                if (base_nca && base_nca->GetStatus() == Loader::ResultStatus::Success) {
                    found = true;
                    break;
                }
            }
        }
        if (!found || !base_nca) {
            LOG_ERROR(Frontend, "Cannot dump RomFS: no base Program NCA for {:016X}", program_id);
            return false;
        }
    }

    const FileSys::NCA update_nca{packed_update_raw, nullptr};
    if (type != FileSys::ContentRecordType::Program ||
        update_nca.GetStatus() != Loader::ResultStatus::ErrorMissingBKTRBaseRomFS ||
        update_nca.GetTitleId() != FileSys::GetUpdateTitleID(title_id)) {
        packed_update_raw = {};
    }

    const auto base_romfs = base_nca->GetRomFS();
    if (!base_romfs) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: base NCA {:016X} has no RomFS", title_id);
        return false;
    }

    const auto dump_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::DumpDir);
    const auto romfs_dir = fmt::format("{:016X}/romfs", title_id);
    const auto path = dump_dir / romfs_dir;

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(), installed};
    auto romfs = pm.PatchRomFS(base_nca.get(), base_romfs, type, packed_update_raw, false);

    if (!romfs) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: failed to patch RomFS for {:016X}", title_id);
        return false;
    }

    const auto extracted = FileSys::ExtractRomFS(romfs);
    if (extracted == nullptr) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: failed to extract RomFS for {:016X}", title_id);
        return false;
    }

    // Create output directory using VFS
    const auto path_str = Common::FS::PathToUTF8String(path);
    const auto out_dir = vfs.CreateDirectory(path_str, FileSys::OpenMode::ReadWrite);
    if (!out_dir || !out_dir->IsWritable()) {
        LOG_ERROR(Frontend, "Cannot dump RomFS: output directory '{}' is not writable", path_str);
        return false;
    }

    // Copy RomFS recursively
    const auto total_size = romfs->GetSize();
    size_t read_size = 0;

    auto jlambdaClass = env->GetObjectClass(jcallback);
    auto jlambdaInvokeMethod = env->GetMethodID(
        jlambdaClass, "invoke", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    // Helper to create Java Long objects
    auto jLongClass = env->FindClass("java/lang/Long");
    auto jLongValueOf = env->GetStaticMethodID(jLongClass, "valueOf", "(J)Ljava/lang/Long;");

    const auto callback = [env, jcallback, jlambdaInvokeMethod, jLongClass, jLongValueOf, &read_size, total_size](
                             size_t current_read) -> bool {
        read_size += current_read;
        auto jmax = env->CallStaticObjectMethod(jLongClass, jLongValueOf, static_cast<jlong>(total_size));
        auto jprogress = env->CallStaticObjectMethod(jLongClass, jLongValueOf, static_cast<jlong>(read_size));
        auto jwasCancelled = env->CallObjectMethod(jcallback, jlambdaInvokeMethod, jmax, jprogress);
        env->DeleteLocalRef(jmax);
        env->DeleteLocalRef(jprogress);
        return Common::Android::GetJBoolean(env, jwasCancelled);
    };

    // Recursive copy function
    const std::function<bool(const FileSys::VirtualDir&, const FileSys::VirtualDir&)> copyDir =
        [&](const FileSys::VirtualDir& src, const FileSys::VirtualDir& dest) -> bool {
            if (!src || !dest) {
                return false;
            }

            // Copy files
            for (const auto& file : src->GetFiles()) {
                if (callback(0)) { // Check for cancellation
                    return false;
                }
                const auto out_file = dest->CreateFile(file->GetName());
                if (!FileSys::VfsRawCopy(file, out_file)) {
                    LOG_ERROR(Frontend, "Cannot dump RomFS: failed to copy '{}'",
                              file->GetFullPath());
                    return false;
                }
                if (callback(file->GetSize())) {
                    return false;
                }
            }

            // Copy subdirectories
            for (const auto& dir : src->GetSubdirectories()) {
                if (callback(0)) {
                    return false;
                }
                const auto out_subdir = dest->CreateSubdirectory(dir->GetName());
                if (!copyDir(dir, out_subdir)) {
                    return false;
                }
            }

            return true;
        };

    return copyDir(extracted, out_dir);
}

jboolean Java_org_citron_citron_1emu_NativeLibrary_dumpExeFS(JNIEnv* env, jobject jobj,
                                                          jstring jgamePath, jstring jprogramId,
                                                          jstring jdumpPath, jobject jcallback) {
    (void)jdumpPath;
    (void)jcallback;
    // Check if emulation is running - dumping while emulation is active can cause crashes
    if (EmulationSession::GetInstance().IsRunning()) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS while emulation is running. Please close the game first.");
        return false;
    }

    const auto game_path = Common::Android::GetJString(env, jgamePath);
    const auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    if (program_id == 0) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: invalid program ID");
        return false;
    }

    auto& system = EmulationSession::GetInstance().System();
    auto& vfs = *system.GetFilesystem();

    const auto game_file = vfs.OpenFile(game_path, FileSys::OpenMode::Read);
    if (game_file == nullptr) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: failed to open game file '{}'", game_path);
        return false;
    }
    const auto loader = Loader::GetLoader(system, game_file);
    if (loader == nullptr) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: no loader for '{}'", game_path);
        return false;
    }

    EmulationSession::GetInstance().ConfigureFilesystemProvider(game_path);

    const auto& installed = system.GetContentProvider();

    // Find the base NCA for the program
    u64 title_id = program_id;
    const auto type = FileSys::ContentRecordType::Program;
    auto base_nca = installed.GetEntry(title_id, type);
    if (!base_nca || base_nca->GetStatus() != Loader::ResultStatus::Success) {
        // Try to find any matching entry
        const auto entries = installed.ListEntriesFilter(FileSys::TitleType::Application, type);
        for (const auto& entry : entries) {
            if (FileSys::GetBaseTitleID(entry.title_id) == program_id) {
                title_id = entry.title_id;
                base_nca = installed.GetEntry(title_id, type);
                if (base_nca && base_nca->GetStatus() == Loader::ResultStatus::Success) {
                    break;
                }
            }
        }
        if (!base_nca || base_nca->GetStatus() != Loader::ResultStatus::Success) {
            LOG_ERROR(Frontend, "Cannot dump ExeFS: no base Program NCA for {:016X}", program_id);
            return false;
        }
    }

    auto exefs = base_nca->GetExeFS();
    if (!exefs) {
        // Try update NCA
        const auto update_nca = installed.GetEntry(FileSys::GetUpdateTitleID(title_id), type);
        if (update_nca) {
            exefs = update_nca->GetExeFS();
        }
        if (!exefs) {
            LOG_ERROR(Frontend, "Cannot dump ExeFS: no ExeFS for {:016X}", title_id);
            return false;
        }
    }

    // Apply patches
    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(), installed};
    exefs = pm.PatchExeFS(exefs);

    if (!exefs) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: failed to patch ExeFS for {:016X}", title_id);
        return false;
    }

    const auto dump_dir = system.GetFileSystemController().GetModificationDumpRoot(title_id);
    if (!dump_dir) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: failed to open dump root for {:016X}", title_id);
        return false;
    }

    const auto exefs_dir = FileSys::GetOrCreateDirectoryRelative(dump_dir, "/exefs");
    if (!exefs_dir) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: failed to create output directory for {:016X}",
                  title_id);
        return false;
    }

    // Copy ExeFS - callback is unused for ExeFS as it's typically small
    if (!FileSys::VfsRawCopyD(exefs, exefs_dir)) {
        LOG_ERROR(Frontend, "Cannot dump ExeFS: failed to copy files for {:016X}", title_id);
        return false;
    }
    return true;
}

} // extern "C"
