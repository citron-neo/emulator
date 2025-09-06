// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <common/settings_common.h>
#include "common/common_types.h"
#include "common/settings_setting.h"

namespace AndroidSettings {

struct GameDir {
    std::string path;
    bool deep_scan = false;
};

struct OverlayControlData {
    std::string id;
    bool enabled;
    std::pair<double, double> landscape_position;
    std::pair<double, double> portrait_position;
    std::pair<double, double> foldable_position;
};

struct Values {
    Settings::Linkage linkage;

    // Path settings
    std::vector<GameDir> game_dirs;

    // Android
    Settings::Setting<bool> picture_in_picture{linkage, false, "picture_in_picture",
                                               Settings::Category::Android};
    Settings::Setting<s32> screen_layout{linkage,
                                         5,
                                         "screen_layout",
                                         Settings::Category::Android,
                                         Settings::Specialization::Default,
                                         true,
                                         true};
    Settings::Setting<s32> vertical_alignment{linkage,
                                              0,
                                              "vertical_alignment",
                                              Settings::Category::Android,
                                              Settings::Specialization::Default,
                                              true,
                                              true};

    Settings::SwitchableSetting<std::string, false> driver_path{linkage, "", "driver_path",
                                                                Settings::Category::GpuDriver};

    Settings::Setting<s32> theme{linkage, 0, "theme", Settings::Category::Android};
    Settings::Setting<s32> theme_mode{linkage, -1, "theme_mode", Settings::Category::Android};
    Settings::Setting<bool> black_backgrounds{linkage, false, "black_backgrounds",
                                              Settings::Category::Android};

    // Input/performance overlay settings
    std::vector<OverlayControlData> overlay_control_data;
    Settings::Setting<s32> overlay_scale{linkage, 50, "control_scale", Settings::Category::Overlay};
    Settings::Setting<s32> overlay_opacity{linkage, 100, "control_opacity",
                                           Settings::Category::Overlay};

    Settings::Setting<bool> joystick_rel_center{linkage, true, "joystick_rel_center",
                                                Settings::Category::Overlay};
    Settings::Setting<bool> dpad_slide{linkage, true, "dpad_slide", Settings::Category::Overlay};
    Settings::Setting<bool> haptic_feedback{linkage, true, "haptic_feedback",
                                            Settings::Category::Overlay};
    Settings::Setting<bool> show_performance_overlay{linkage, true, "show_performance_overlay",
                                                     Settings::Category::Overlay};
    Settings::Setting<bool> show_thermal_overlay{linkage, false, "show_thermal_overlay",
                                                 Settings::Category::Overlay};
    Settings::Setting<bool> show_ram_meter{linkage, false, "show_ram_meter",
                                           Settings::Category::Overlay};
    Settings::Setting<bool> show_shader_building_overlay{linkage, true, "show_shader_building_overlay",
                                                         Settings::Category::Overlay};
    Settings::Setting<bool> show_performance_graph{linkage, false, "show_performance_graph",
                                                   Settings::Category::Overlay};
    Settings::Setting<bool> show_input_overlay{linkage, true, "show_input_overlay",
                                               Settings::Category::Overlay};
    Settings::Setting<bool> touchscreen{linkage, true, "touchscreen", Settings::Category::Overlay};
    Settings::Setting<s32> lock_drawer{linkage, false, "lock_drawer", Settings::Category::Overlay};
};

extern Values values;

} // namespace AndroidSettings
