# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(tests
    common/bit_field.cpp
    common/cityhash.cpp
    common/container_hash.cpp
    common/fibers.cpp
    common/host_memory.cpp
    common/logging.cpp
    common/param_package.cpp
    common/range_map.cpp
    common/ring_buffer.cpp
    common/scratch_buffer.cpp
    common/unique_function.cpp
    common/uint128.cpp
    core/aes_util.cpp
    core/core_timing.cpp
    core/exit_callback.cpp
    core/internal_network/network.cpp
    precompiled_headers.h
    video_core/blit_operation.cpp
    video_core/memory_tracker.cpp
    video_core/image_size.cpp
    video_core/cache_memory_pressure.cpp
    video_core/descriptor_bank.cpp
    video_core/dma_remap.cpp
    video_core/syncpoint_manager.cpp
    video_core/uma.cpp
    video_core/direct_guest_mapping.cpp
    input_common/calibration_configuration_job.cpp
)

# Test translation units are not unity-safe.
set_property(TARGET tests PROPERTY UNITY_BUILD OFF)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core input_common)
# video_core/descriptor_bank.cpp includes the Vulkan backend's headers.
target_link_libraries(tests PRIVATE Vulkan::Headers)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)

add_test(NAME tests COMMAND tests)

# Full native owners and software Vulkan; separate from portable unit tests so a missing
# graphics driver is an explicit CI failure rather than a silently skipped acceptance.
add_executable(controlled_cpu_scheduler_tests core/controlled_cpu_scheduler.cpp)
set_property(TARGET controlled_cpu_scheduler_tests PROPERTY UNITY_BUILD OFF)
target_link_libraries(controlled_cpu_scheduler_tests PRIVATE common core Vulkan::Headers
    ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)
add_test(NAME controlled_cpu_scheduler_tests COMMAND controlled_cpu_scheduler_tests)
set_tests_properties(controlled_cpu_scheduler_tests PROPERTIES TIMEOUT 180)
# A new System in the same host thread would reuse KernelCore's TLS dummy KThread
# from the first System. Keep both native owners intact and isolate their processes.
add_test(NAME native_f3_witness COMMAND controlled_cpu_scheduler_tests "[f3]")
set_tests_properties(native_f3_witness PROPERTIES TIMEOUT 30)
add_test(NAME native_f3_transfer COMMAND controlled_cpu_scheduler_tests "[f3-transfer]")
set_tests_properties(native_f3_transfer PROPERTIES TIMEOUT 30)
add_test(NAME native_f3_provenance COMMAND controlled_cpu_scheduler_tests "[f3-provenance]")
set_tests_properties(native_f3_provenance PROPERTIES TIMEOUT 30)

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(tests PRIVATE precompiled_headers.h)
endif()
