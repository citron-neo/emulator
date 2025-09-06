# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(hid_core STATIC
    frontend/emulated_console.cpp
    frontend/emulated_console.h
    frontend/emulated_controller.cpp
    frontend/emulated_controller.h
    frontend/emulated_devices.cpp
    frontend/emulated_devices.h
    frontend/input_converter.cpp
    frontend/input_converter.h
    frontend/input_interpreter.cpp
    frontend/input_interpreter.h
    frontend/motion_input.cpp
    frontend/motion_input.h
    hidbus/hidbus_base.cpp
    hidbus/hidbus_base.h
    hidbus/ringcon.cpp
    hidbus/ringcon.h
    hidbus/starlink.cpp
    hidbus/starlink.h
    hidbus/stubbed.cpp
    hidbus/stubbed.h
    irsensor/clustering_processor.cpp
    irsensor/clustering_processor.h
    irsensor/image_transfer_processor.cpp
    irsensor/image_transfer_processor.h
    irsensor/ir_led_processor.cpp
    irsensor/ir_led_processor.h
    irsensor/moment_processor.cpp
    irsensor/moment_processor.h
    irsensor/pointing_processor.cpp
    irsensor/pointing_processor.h
    irsensor/processor_base.cpp
    irsensor/processor_base.h
    irsensor/tera_plugin_processor.cpp
    irsensor/tera_plugin_processor.h
    resources/abstracted_pad/abstract_battery_handler.cpp
    resources/abstracted_pad/abstract_battery_handler.h
    resources/abstracted_pad/abstract_button_handler.cpp
    resources/abstracted_pad/abstract_button_handler.h
    resources/abstracted_pad/abstract_ir_sensor_handler.cpp
    resources/abstracted_pad/abstract_ir_sensor_handler.h
    resources/abstracted_pad/abstract_led_handler.cpp
    resources/abstracted_pad/abstract_led_handler.h
    resources/abstracted_pad/abstract_mcu_handler.cpp
    resources/abstracted_pad/abstract_mcu_handler.h
    resources/abstracted_pad/abstract_nfc_handler.cpp
    resources/abstracted_pad/abstract_nfc_handler.h
    resources/abstracted_pad/abstract_pad.cpp
    resources/abstracted_pad/abstract_pad.h
    resources/abstracted_pad/abstract_pad_holder.cpp
    resources/abstracted_pad/abstract_pad_holder.h
    resources/abstracted_pad/abstract_palma_handler.cpp
    resources/abstracted_pad/abstract_palma_handler.h
    resources/abstracted_pad/abstract_properties_handler.cpp
    resources/abstracted_pad/abstract_properties_handler.h
    resources/abstracted_pad/abstract_sixaxis_handler.cpp
    resources/abstracted_pad/abstract_sixaxis_handler.h
    resources/abstracted_pad/abstract_vibration_handler.cpp
    resources/abstracted_pad/abstract_vibration_handler.h
    resources/debug_pad/debug_pad.cpp
    resources/debug_pad/debug_pad.h
    resources/debug_pad/debug_pad_types.h
    resources/digitizer/digitizer.cpp
    resources/digitizer/digitizer.h
    resources/keyboard/keyboard.cpp
    resources/keyboard/keyboard.h
    resources/keyboard/keyboard_types.h
    resources/mouse/debug_mouse.cpp
    resources/mouse/debug_mouse.h
    resources/mouse/mouse.cpp
    resources/mouse/mouse.h
    resources/mouse/mouse_types.h
    resources/npad/npad.cpp
    resources/npad/npad.h
    resources/npad/npad_data.cpp
    resources/npad/npad_data.h
    resources/npad/npad_resource.cpp
    resources/npad/npad_resource.h
    resources/npad/npad_types.h
    resources/npad/npad_vibration.cpp
    resources/npad/npad_vibration.h
    resources/palma/palma.cpp
    resources/palma/palma.h
    resources/six_axis/console_six_axis.cpp
    resources/six_axis/console_six_axis.h
    resources/six_axis/seven_six_axis.cpp
    resources/six_axis/seven_six_axis.h
    resources/six_axis/six_axis.cpp
    resources/six_axis/six_axis.h
    resources/system_buttons/capture_button.cpp
    resources/system_buttons/capture_button.h
    resources/system_buttons/home_button.cpp
    resources/system_buttons/home_button.h
    resources/system_buttons/sleep_button.cpp
    resources/system_buttons/sleep_button.h
    resources/system_buttons/system_button_types.h
    resources/touch_screen/gesture.cpp
    resources/touch_screen/gesture.h
    resources/touch_screen/gesture_handler.cpp
    resources/touch_screen/gesture_handler.h
    resources/touch_screen/touch_screen.cpp
    resources/touch_screen/touch_screen.h
    resources/touch_screen/touch_screen_driver.cpp
    resources/touch_screen/touch_screen_driver.h
    resources/touch_screen/touch_screen_resource.cpp
    resources/touch_screen/touch_screen_resource.h
    resources/touch_screen/touch_types.h
    resources/unique_pad/unique_pad.cpp
    resources/unique_pad/unique_pad.h
    resources/vibration/gc_vibration_device.h
    resources/vibration/gc_vibration_device.cpp
    resources/vibration/n64_vibration_device.h
    resources/vibration/n64_vibration_device.cpp
    resources/vibration/vibration_base.h
    resources/vibration/vibration_base.cpp
    resources/vibration/vibration_device.h
    resources/vibration/vibration_device.cpp
    resources/applet_resource.cpp
    resources/applet_resource.h
    resources/controller_base.cpp
    resources/controller_base.h
    resources/hid_firmware_settings.cpp
    resources/hid_firmware_settings.h
    resources/irs_ring_lifo.h
    resources/ring_lifo.h
    resources/shared_memory_format.h
    resources/shared_memory_holder.cpp
    resources/shared_memory_holder.h
    hid_core.cpp
    hid_core.h
    hid_result.h
    hid_types.h
    hid_util.h
    precompiled_headers.h
    resource_manager.cpp
    resource_manager.h
)

if (MSVC)
    target_compile_options(hid_core PRIVATE
        /we4242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
        /we4244 # 'conversion': conversion from 'type1' to 'type2', possible loss of data
        /we4245 # 'conversion': conversion from 'type1' to 'type2', signed/unsigned mismatch
        /we4254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    )
else()
    target_compile_options(hid_core PRIVATE
        -Werror=conversion

        -Wno-sign-conversion
        -Wno-cast-function-type

        $<$<CXX_COMPILER_ID:Clang>:-fsized-deallocation>
    )
endif()

create_target_directory_groups(hid_core)
target_link_libraries(hid_core PUBLIC core)

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(hid_core PRIVATE precompiled_headers.h)
endif()

citron_configure_lto(hid_core)
