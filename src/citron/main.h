// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <filesystem>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTranslator>
#include "common/announce_multiplayer_room.h"
#include "common/common_types.h"
#include "configuration/qt_config.h"
#include "frontend_common/content_manager.h"
#include "input_common/drivers/tas_input.h"
#include "citron/compatibility_list.h"
#include "citron/hotkeys.h"
#include "citron/util/controller_navigation.h"

#ifdef __unix__
#include <QVariant>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QtDBus>
#endif

class QtConfig;
class ClickableLabel;
class EmuThread;
class GameList;
class GImageInfo;
class GRenderWindow;
class LoadingScreen;
class MicroProfileDialog;
class OverlayDialog;
class PerformanceOverlay;
class MultiplayerRoomOverlay;
class VramOverlay;
class ControllerOverlay;
class ProfilerWidget;
class ControllerDialog;
class QLabel;
class MultiplayerState;
class QPushButton;
class QProgressDialog;
class QSlider;
class QHBoxLayout;
class WaitTreeWidget;
enum class GameListOpenTarget;
enum class GameListRemoveTarget;
enum class GameListShortcutTarget;
enum class DumpRomFSTarget;
enum class InstalledEntryType;
class GameListPlaceholder;
class QtAmiiboSettingsDialog;
class QtControllerSelectorDialog;
class QtProfileSelectionDialog;
class QtSoftwareKeyboardDialog;
class QtNXWebEngineView;
namespace Updater { class UpdaterDialog; }

enum class StartGameType { Normal, Global };

namespace Core { enum class SystemResultStatus : u32; class System; }
namespace Core::Frontend { struct CabinetParameters; struct ControllerParameters; struct InlineAppearParameters; struct InlineTextParameters; struct KeyboardInitializeParameters; struct ProfileSelectParameters; }
namespace DiscordRPC { class DiscordInterface; }
namespace PlayTime { class PlayTimeManager; }
namespace FileSys { class ContentProvider; class ManualContentProvider; class VfsFilesystem; }
namespace InputCommon { class InputSubsystem; }
namespace Service::AM { struct FrontendAppletParameters; enum class AppletId : u32; }
namespace Service::AM::Frontend { enum class SwkbdResult : u32; enum class SwkbdTextCheckResult : u32; enum class SwkbdReplyType : u32; enum class WebExitReason : u32; }
namespace Service::NFC { class NfcDevice; }
namespace Service::NFP { enum class CabinetMode : u8; }
namespace Ui { class MainWindow; }
enum class EmulatedDirectoryTarget { NAND, SDMC };
namespace VkDeviceInfo { class Record; }

class VolumeButton : public QPushButton {
    Q_OBJECT
public:
    explicit VolumeButton(QWidget* parent = nullptr) : QPushButton(parent), scroll_multiplier(1) {
        connect(&scroll_timer, &QTimer::timeout, this, &VolumeButton::ResetMultiplier);
    }
signals:
    void VolumeChanged();
protected:
    void wheelEvent(QWheelEvent* event) override;
private slots:
    void ResetMultiplier();
private:
    int scroll_multiplier;
    QTimer scroll_timer;
    constexpr static int MaxMultiplier = 8;
};

class GMainWindow : public QMainWindow {
    Q_OBJECT
    static const int max_recent_files_item = 10;
    friend class PerformanceOverlay;
    friend class VramOverlay;
    enum { CREATE_SHORTCUT_MSGBOX_FULLSCREEN_YES, CREATE_SHORTCUT_MSGBOX_SUCCESS, CREATE_SHORTCUT_MSGBOX_ERROR, CREATE_SHORTCUT_MSGBOX_APPVOLATILE_WARNING };
public:
    void filterBarSetChecked(bool state);
    void UpdateUITheme();
    explicit GMainWindow(std::unique_ptr<QtConfig> config_, bool has_broken_vulkan);
    ~GMainWindow() override;
    bool DropAction(QDropEvent* event);
    void AcceptDropEvent(QDropEvent* event);
    MultiplayerState* GetMultiplayerState() { return multiplayer_state; }
    Core::System* GetSystem() { return system.get(); }
    const std::shared_ptr<FileSys::VfsFilesystem>& GetVFS() const { return vfs; }
    bool IsEmulationRunning() const { return emulation_running; }
    void RefreshGameList();
    GRenderWindow* GetRenderWindow() const { return render_window; }
    bool ExtractZipToDirectoryPublic(const std::filesystem::path& zip_path, const std::filesystem::path& extract_path);
signals:
    void EmulationStarting(EmuThread* emu_thread);
    void EmulationStopping();
    void UpdateThemedIcons();
    void themeChanged();
    void UpdateInstallProgress();
    void AmiiboSettingsFinished(bool is_success, const std::string& name);
    void ControllerSelectorReconfigureFinished(bool is_success);
    void ErrorDisplayFinished();
    void ProfileSelectorFinishedSelection(std::optional<Common::UUID> uuid);
    void SoftwareKeyboardSubmitNormalText(Service::AM::Frontend::SwkbdResult result, std::u16string submitted_text, bool confirmed);
    void SoftwareKeyboardSubmitInlineText(Service::AM::Frontend::SwkbdReplyType reply_type, std::u16string submitted_text, s32 cursor_position);
    void WebBrowserExtractOfflineRomFS();
    void WebBrowserClosed(Service::AM::Frontend::WebExitReason exit_reason, std::string last_url);
    void SigInterrupt();
public slots:
    void OnLoadComplete();
    void OnExecuteProgram(std::size_t program_index);
    void OnExit();
    void OnSaveConfig();
    void AmiiboSettingsShowDialog(const Core::Frontend::CabinetParameters& parameters, std::shared_ptr<Service::NFC::NfcDevice> nfp_device);
    void AmiiboSettingsRequestExit();
    void ControllerSelectorReconfigureControllers(const Core::Frontend::ControllerParameters& parameters);
    void ControllerSelectorRequestExit();
    void SoftwareKeyboardInitialize(bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters);
    void SoftwareKeyboardShowNormal();
    void SoftwareKeyboardShowTextCheck(Service::AM::Frontend::SwkbdTextCheckResult text_check_result, std::u16string text_check_message);
    void SoftwareKeyboardShowInline(Core::Frontend::InlineAppearParameters appear_parameters);
    void SoftwareKeyboardHideInline();
    void SoftwareKeyboardInlineTextChanged(Core::Frontend::InlineTextParameters text_parameters);
    void SoftwareKeyboardExit();
    void ErrorDisplayDisplayError(QString error_code, QString error_text);
    void ErrorDisplayRequestExit();
    void ProfileSelectorSelectProfile(const Core::Frontend::ProfileSelectParameters& parameters);
    void ProfileSelectorRequestExit();
    void WebBrowserOpenWebPage(const std::string& main_url, const std::string& additional_args, bool is_local);
    void WebBrowserRequestExit();
    void OnAppFocusStateChanged(Qt::ApplicationState state);
    void OnTasStateChanged();
    void IncrementInstallProgress();
private:
    void LinkActionShortcut(QAction* action, const QString& action_name, const bool tas_allowed = false);
    void RegisterMetaTypes();
    void RegisterAutoloaderContents();
    void InitializeWidgets();
    void InitializeDebugWidgets();
    void InitializeRecentFileMenuActions();
    void SetDefaultUIGeometry();
    void RestoreUIState();
    void ConnectWidgetEvents();
    void ConnectMenuEvents();
    void UpdateMenuState();
    void SetupPrepareForSleep();
    void PreventOSSleep();
    void AllowOSSleep();
    bool LoadROM(const QString& filename, Service::AM::FrontendAppletParameters params);
    void BootGame(const QString& filename, Service::AM::FrontendAppletParameters params, StartGameType with_config = StartGameType::Normal);
    void BootGameFromList(const QString& filename, StartGameType with_config);
    void ShutdownGame();
    void ShowTelemetryCallout();
    void SetDiscordEnabled(bool state);
    void LoadAmiibo(const QString& filename);
    bool SelectAndSetCurrentUser(const Core::Frontend::ProfileSelectParameters& parameters);
    void StoreRecentFile(const QString& filename);
    void UpdateRecentFiles();
    bool ConfirmClose();
    bool ConfirmChangeGame();
    bool ConfirmForceLockedExit();
    void RequestGameExit();
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    std::string CreateTASFramesString(std::array<size_t, InputCommon::TasInput::PLAYER_NUMBER> frames) const;
    #ifdef __unix__
    void SetupSigInterrupts();
    static void HandleSigInterrupt(int);
    void OnSigInterruptNotifierActivated();
    void SetGamemodeEnabled(bool state);
    #endif
    Service::AM::FrontendAppletParameters ApplicationAppletParameters();
    Service::AM::FrontendAppletParameters LibraryAppletParameters(u64 program_id, Service::AM::AppletId applet_id);
    std::unique_ptr<FileSys::ManualContentProvider> autoloader_provider;
private slots:
    void OnStartGame();
    void OnRestartGame();
    void OnPauseGame();
    void OnPauseContinueGame();
    void OnStopGame();
    void OnPrepareForSleep(bool prepare_sleep);
    void OnMenuReportCompatibility();
    void OnOpenSupport();
    void OnGameListLoadFile(QString game_path, u64 program_id);
    void OnGameListOpenFolder(u64 program_id, GameListOpenTarget target, const std::string& game_path);
    void OnTransferableShaderCacheOpenFile(u64 program_id);
    void OnGameListRemoveInstalledEntry(u64 program_id, InstalledEntryType type);
    void OnGameListRemoveFile(u64 program_id, GameListRemoveTarget target, const std::string& game_path);
    void OnGameListRemovePlayTimeData(u64 program_id);
    void OnGameListDumpRomFS(u64 program_id, const std::string& game_path, DumpRomFSTarget target);
    void OnGameListVerifyIntegrity(const std::string& game_path);
    void OnGameListCopyTID(u64 program_id);
    void OnGameListNavigateToGamedbEntry(u64 program_id, const CompatibilityList& compatibility_list);
    void OnGameListCreateShortcut(u64 program_id, const std::string& game_path, GameListShortcutTarget target);
    void OnGameListOpenDirectory(const QString& directory);
    void OnGameListAddDirectory();
    void OnGameListShowList(bool show);
    void OnGameListOpenPerGameProperties(const std::string& file);
    void OnMenuLoadFile();
    void OnMenuLoadFolder();
    void OnMenuInstallToNAND();
    void OnMenuTrimXCI();
    void OnMenuInstallWithUpdateManager();
    void OnRunAutoloaderFromGameList();
    void OnMenuRecentFile();
    void OnConfigure();
    void OnConfigureTas();
    void OnDecreaseVolume();
    void OnIncreaseVolume();
    void OnMute();
    void OnTasStartStop();
    void OnTasRecord();
    void OnTasReset();
    void OnToggleGraphicsAPI();
    void OnToggleDockedMode();
    void OnToggleGpuAccuracy();
    void OnToggleAdaptingFilter();
    void OnConfigurePerGame();
    void OnLoadAmiibo();
    void OnOpenCitronFolder();
    void OnVerifyInstalledContents();
    void OnInstallFirmware();
    void OnInstallFirmwareFromZip();
    bool ExtractZipToDirectory(const std::filesystem::path& zip_path, const std::filesystem::path& extract_path);
    void OnInstallDecryptionKeys();
    void OnAbout();
    void OnCheckForUpdates();
    void CheckForUpdatesAutomatically();
    void OnToggleFilterBar();
    void OnToggleGridView();
    void OnToggleStatusBar();
    void OnTogglePerformanceOverlay();
    void OnToggleMultiplayerRoomOverlay();
    void OnToggleVramOverlay();
    void OnToggleControllerOverlay();
    void OnDisplayTitleBars(bool);
    double GetCurrentFPS() const;
    double GetCurrentFrameTime() const;
    u32 GetShadersBuilding() const;
    double GetEmulationSpeed() const;
    u64 GetTotalVram() const;
    u64 GetUsedVram() const;
    u64 GetBufferMemoryUsage() const;
    u64 GetTextureMemoryUsage() const;
    u64 GetStagingMemoryUsage() const;
    void InitializeHotkeys();
    void ToggleFullscreen();
    bool UsingExclusiveFullscreen();
    void ShowFullscreen();
    void HideFullscreen();
    void ToggleWindowMode();
    void ResetWindowSize(u32 width, u32 height);
    void ResetWindowSize720();
    void ResetWindowSize900();
    void ResetWindowSize1080();
    void OnAlbum();
    void OnCabinet(Service::NFP::CabinetMode mode);
    void OnMiiEdit();
    void OnOpenControllerMenu();
    void OnCaptureScreenshot();
    void OnCheckFirmwareDecryption();
    void OnLanguageChanged(const QString& locale);
    void OnMouseActivity();
    bool OnShutdownBegin();
    void OnShutdownBeginDialog();
    void OnEmulationStopped();
    void OnEmulationStopTimeExpired();
private:
    QString GetGameListErrorRemoving(InstalledEntryType type) const;
    void RemoveBaseContent(u64 program_id, InstalledEntryType type);
    void RemoveUpdateContent(u64 program_id, InstalledEntryType type);
    void RemoveAddOnContent(u64 program_id, InstalledEntryType type);
    void RemoveTransferableShaderCache(u64 program_id, GameListRemoveTarget target);
    void RemoveVulkanDriverPipelineCache(u64 program_id);
    void RemoveAllTransferableShaderCaches(u64 program_id);
    void RemoveCustomConfiguration(u64 program_id, const std::string& game_path);
    void RemovePlayTimeData(u64 program_id);
    void RemoveCacheStorage(u64 program_id);
    bool SelectRomFSDumpTarget(const FileSys::ContentProvider&, u64 program_id, u64* selected_title_id, u8* selected_content_record_type);
    ContentManager::InstallResult InstallNCA(const QString& filename);
    void MigrateConfigFiles();
    void UpdateWindowTitle(std::string_view title_name = {}, std::string_view title_version = {}, std::string_view gpu_vendor = {});
    void UpdateDockedButton();
    void UpdateAPIText();
    void UpdateFilterText();
    void UpdateAAText();
    void UpdateVolumeUI();
    void UpdateStatusBar();
    void UpdateGPUAccuracyButton();
    void UpdateStatusButtons();
    void UpdateUISettings();
    void UpdateInputDrivers();
    void HideMouseCursor();
    void ShowMouseCursor();
    void OpenURL(const QUrl& url);
    void LoadTranslation();
    void OpenPerGameConfiguration(u64 title_id, const std::string& file_name);
    bool CheckDarkMode();
    bool CheckFirmwarePresence();
    void SetFirmwareVersion();
    void ConfigureFilesystemProvider(const std::string& filepath);
    bool ConfirmShutdownGame();
    QString GetTasStateDescription() const;
    bool CreateShortcutMessagesGUI(QWidget* parent, int imsg, const QString& game_title);
    bool MakeShortcutIcoPath(const u64 program_id, const std::string_view game_file_name, std::filesystem::path& out_icon_path);
    bool CreateShortcutLink(const std::filesystem::path& shortcut_path, const std::string& comment, const std::filesystem::path& icon_path, const std::filesystem::path& command, const std::string& arguments, const std::string& categories, const std::string& keywords, const std::string& name);
    bool question(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No), QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
    std::unique_ptr<Ui::MainWindow> ui;
    std::unique_ptr<Core::System> system;
    std::unique_ptr<DiscordRPC::DiscordInterface> discord_rpc;
    std::unique_ptr<PlayTime::PlayTimeManager> play_time_manager;
    std::shared_ptr<InputCommon::InputSubsystem> input_subsystem;
    MultiplayerState* multiplayer_state = nullptr;
    GRenderWindow* render_window;
    GameList* game_list;
    LoadingScreen* loading_screen;
    QTimer shutdown_timer;
    OverlayDialog* shutdown_dialog{};
    PerformanceOverlay* performance_overlay{};
    MultiplayerRoomOverlay* multiplayer_room_overlay{};
    VramOverlay* vram_overlay{};
    ControllerOverlay* controller_overlay{};
    GameListPlaceholder* game_list_placeholder;
    std::vector<VkDeviceInfo::Record> vk_device_records;
    QLabel* message_label = nullptr;
    QLabel* shader_building_label = nullptr;
    QLabel* res_scale_label = nullptr;
    QLabel* emu_speed_label = nullptr;
    QLabel* game_fps_label = nullptr;
    QLabel* emu_frametime_label = nullptr;
    QLabel* tas_label = nullptr;
    QLabel* firmware_label = nullptr;
    QPushButton* gpu_accuracy_button = nullptr;
    QPushButton* renderer_status_button = nullptr;
    QPushButton* dock_status_button = nullptr;
    QPushButton* filter_status_button = nullptr;
    QPushButton* aa_status_button = nullptr;
    VolumeButton* volume_button = nullptr;
    QWidget* volume_popup = nullptr;
    QSlider* volume_slider = nullptr;
    QTimer status_bar_update_timer;
    std::unique_ptr<QtConfig> config;
    bool emulation_running = false;
    std::unique_ptr<EmuThread> emu_thread;
    QString current_game_path;
    bool user_flag_cmd_line = false;
    bool auto_paused = false;
    bool auto_muted = false;
    QTimer mouse_hide_timer;
    QTimer update_input_timer;
    QString startup_icon_theme;
    bool os_dark_mode = false;
    std::shared_ptr<FileSys::VfsFilesystem> vfs;
    std::unique_ptr<FileSys::ManualContentProvider> provider;
    ProfilerWidget* profilerWidget;
    MicroProfileDialog* microProfileDialog;
    WaitTreeWidget* waitTreeWidget;
    ControllerDialog* controller_dialog;
    QAction* actions_recent_files[max_recent_files_item];
    QStringList default_theme_paths;
    HotkeyRegistry hotkey_registry;
    QTranslator translator;
    QProgressDialog* install_progress;
    QString last_filename_booted;
    QtAmiiboSettingsDialog* cabinet_applet = nullptr;
    QtControllerSelectorDialog* controller_applet = nullptr;
    QtProfileSelectionDialog* profile_select_applet = nullptr;
    QDialog* error_applet = nullptr;
    QtSoftwareKeyboardDialog* software_keyboard = nullptr;
    QtNXWebEngineView* web_applet = nullptr;
    QAction* action_exit_fullscreen;
    bool is_amiibo_file_select_active{};
    bool is_load_file_select_active{};
    bool is_tas_recording_dialog_active{};
    bool m_is_updating_theme = false;
    #ifdef __unix__
    QSocketNotifier* sig_interrupt_notifier;
    static std::array<int, 3> sig_interrupt_fds;
    #endif
protected:
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
};
