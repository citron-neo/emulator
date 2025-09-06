// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include "common/settings.h"

#define SI_NO_CONVERSION
#include <SimpleIni.h>
#include <boost/algorithm/string/replace.hpp>

// Workaround for conflicting definition in libloaderapi.h caused by SimpleIni
#undef LoadString
#undef CreateFile
#undef DeleteFile
#undef CopyFile
#undef CreateDirectory
#undef MoveFile

namespace Core {
class System;
}

class Config {
public:
    enum class ConfigType {
        GlobalConfig,
        PerGameConfig,
        InputProfile,
    };

    virtual ~Config() = default;

    void ClearControlPlayerValues() const;

    [[nodiscard]] const std::string& GetConfigFilePath() const;

    [[nodiscard]] bool Exists(const std::string& section, const std::string& key) const;

protected:
    explicit Config(ConfigType config_type = ConfigType::GlobalConfig);

    void Initialize(const std::string& config_name = "config");
    void Initialize(std::optional<std::string> config_path);

    void WriteToIni() const;

    void SetUpIni();
    [[nodiscard]] bool IsCustomConfig() const;

    void Reload();

    /**
     * Derived config classes must implement this so they can reload all platform-specific
     * values and global ones.
     */
    virtual void ReloadAllValues() = 0;

    /**
     * Derived config classes must implement this so they can save all platform-specific
     * and global values.
     */
    virtual void SaveAllValues() = 0;

    void ReadValues();
    void ReadPlayerValues(std::size_t player_index);

    void ReadTouchscreenValues();
    void ReadMotionTouchValues();

    // Read functions bases off the respective config section names.
    void ReadAudioValues();
    void ReadControlValues();
    void ReadCoreValues();
    void ReadDataStorageValues();
    void ReadDebuggingValues();
#ifdef __unix__
    void ReadLinuxValues();
#endif
    void ReadServiceValues();
    void ReadDisabledAddOnValues();
    void ReadMiscellaneousValues();
    void ReadCpuValues();
    void ReadRendererValues();
    void ReadScreenshotValues();
    void ReadSystemValues();
    void ReadWebServiceValues();
    void ReadNetworkValues();
    void ReadLibraryAppletValues();

    // Read platform specific sections
    virtual void ReadHidbusValues() = 0;
    virtual void ReadDebugControlValues() = 0;
    virtual void ReadPathValues() = 0;
    virtual void ReadShortcutValues() = 0;
    virtual void ReadUIValues() = 0;
    virtual void ReadUIGamelistValues() = 0;
    virtual void ReadUILayoutValues() = 0;
    virtual void ReadMultiplayerValues() = 0;

    void SaveValues();
    void SavePlayerValues(std::size_t player_index);
    void SaveTouchscreenValues();
    void SaveMotionTouchValues();

    // Save functions based off the respective config section names.
    void SaveAudioValues();
    void SaveControlValues();
    void SaveCoreValues();
    void SaveDataStorageValues();
    void SaveDebuggingValues();
#ifdef __unix__
    void SaveLinuxValues();
#endif
    void SaveNetworkValues();
    void SaveDisabledAddOnValues();
    void SaveMiscellaneousValues();
    void SaveCpuValues();
    void SaveRendererValues();
    void SaveScreenshotValues();
    void SaveSystemValues();
    void SaveWebServiceValues();
    void SaveLibraryAppletValues();

    // Save platform specific sections
    virtual void SaveHidbusValues() = 0;
    virtual void SaveDebugControlValues() = 0;
    virtual void SavePathValues() = 0;
    virtual void SaveShortcutValues() = 0;
    virtual void SaveUIValues() = 0;
    virtual void SaveUIGamelistValues() = 0;
    virtual void SaveUILayoutValues() = 0;
    virtual void SaveMultiplayerValues() = 0;

    virtual std::vector<Settings::BasicSetting*>& FindRelevantList(Settings::Category category) = 0;

    /**
     * Reads a setting from the qt_config.
     *
     * @param key The setting's identifier
     * @param default_value The value to use when the setting is not already present in the config
     */
    bool ReadBooleanSetting(const std::string& key,
                            std::optional<bool> default_value = std::nullopt);
    s64 ReadIntegerSetting(const std::string& key, std::optional<s64> default_value = std::nullopt);
    u64 ReadUnsignedIntegerSetting(const std::string& key,
                                   std::optional<u64> default_value = std::nullopt);
    double ReadDoubleSetting(const std::string& key,
                             std::optional<double> default_value = std::nullopt);
    std::string ReadStringSetting(const std::string& key,
                                  std::optional<std::string> default_value = std::nullopt);

    /**
     * Writes a setting to the qt_config.
     *
     * @param key The setting's idetentifier
     * @param value Value of the setting
     * @param default_value Default of the setting if not present in config
     * @param use_global Specifies if the custom or global config should be in use, for custom
     * configs
     */
    void WriteBooleanSetting(const std::string& key, const bool& value,
                             const std::optional<bool>& default_value = std::nullopt,
                             const std::optional<bool>& use_global = std::nullopt);
    void WriteDoubleSetting(const std::string& key, const double& value,
                            const std::optional<double>& default_value = std::nullopt,
                            const std::optional<bool>& use_global = std::nullopt);
    void WriteStringSetting(const std::string& key, const std::string& value,
                            const std::optional<std::string>& default_value = std::nullopt,
                            const std::optional<bool>& use_global = std::nullopt);
    template <typename T>
    std::enable_if_t<std::is_integral_v<T>> WriteIntegerSetting(
        const std::string& key, const T& value,
        const std::optional<T>& default_value = std::nullopt,
        const std::optional<bool>& use_global = std::nullopt) {
        std::optional<std::string> string_default = std::nullopt;
        if (default_value.has_value()) {
            string_default = std::make_optional(ToString(default_value.value()));
        }
        WritePreparedSetting(key, AdjustOutputString(ToString(value)), string_default, use_global);
    }

    void ReadCategory(Settings::Category category);
    void WriteCategory(Settings::Category category);
    void ReadSettingGeneric(Settings::BasicSetting* setting);
    void WriteSettingGeneric(const Settings::BasicSetting* setting);

    template <typename T>
    [[nodiscard]] std::string ToString(const T& value_) {
        if constexpr (std::is_same_v<T, std::string>) {
            return value_;
        } else if constexpr (std::is_same_v<T, std::optional<u32>>) {
            return value_.has_value() ? std::to_string(*value_) : "none";
        } else if constexpr (std::is_same_v<T, bool>) {
            return value_ ? "true" : "false";
        } else if constexpr (std::is_same_v<T, u64>) {
            return std::to_string(static_cast<u64>(value_));
        } else if constexpr (std::is_same_v<T, s64>) {
            return std::to_string(static_cast<s64>(value_));
        } else {
            return std::to_string(value_);
        }
    }

    void BeginGroup(const std::string& group);
    void EndGroup();
    std::string GetSection();
    [[nodiscard]] std::string GetGroup() const;
    static std::string AdjustKey(const std::string& key);
    static std::string AdjustOutputString(const std::string& string);
    std::string GetFullKey(const std::string& key, bool skipArrayIndex);
    int BeginArray(const std::string& array);
    void EndArray();
    void SetArrayIndex(int index);

    const ConfigType type;
    std::unique_ptr<CSimpleIniA> config;
    std::string config_loc;
    const bool global;

private:
    void WritePreparedSetting(const std::string& key, const std::string& adjusted_value,
                              const std::optional<std::string>& adjusted_default_value,
                              const std::optional<bool>& use_global);
    void WriteString(const std::string& key, const std::string& value);

    inline static std::array<char, 18> special_characters = {
        '!', '#', '$', '%', '^', '&', '*', '|', ';', '\'', '\"', ',', '<', '>', '?', '`', '~', '='};

    struct ConfigArray {
        std::string name;
        int size;
        int index;
    };
    std::vector<ConfigArray> array_stack;
    std::vector<std::string> key_stack;
};
