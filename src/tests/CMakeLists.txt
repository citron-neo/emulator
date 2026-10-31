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
    video_core/depth_stencil.cpp
    video_core/depth_stencil_staging.cpp
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

if (TARGET dynarmic)
    get_target_property(_pinned_container_compat dynarmic CITRON_PINNED_CONTAINER_COMPAT)
    if (_pinned_container_compat)
        target_sources(tests PRIVATE common/dynarmic_containers.cpp)
        target_link_libraries(tests PRIVATE dynarmic::dynarmic)
        # The real descriptor type is third-party code with its own warning policy.
        # Keep our test and compatibility headers non-system and under -Werror.
        target_include_directories(tests SYSTEM PRIVATE
            "${CMAKE_SOURCE_DIR}/externals/dynarmic/src/dynarmic/..")
    endif()
endif()

add_test(NAME tests COMMAND tests)

if(WIN32)
    add_executable(windows_mapping_owner_tests common/windows_mapping_owner.cpp)
    set_property(TARGET windows_mapping_owner_tests PROPERTY UNITY_BUILD OFF)
    target_link_libraries(windows_mapping_owner_tests PRIVATE common ${PLATFORM_LIBRARIES} Threads::Threads)
    add_test(NAME windows_mapping_owner COMMAND windows_mapping_owner_tests)
    set_tests_properties(windows_mapping_owner PROPERTIES TIMEOUT 30)
    add_executable(windows_alias_freeze_tests common/windows_alias_freeze.cpp)
    set_property(TARGET windows_alias_freeze_tests PROPERTY UNITY_BUILD OFF)
    target_link_libraries(windows_alias_freeze_tests PRIVATE common ${PLATFORM_LIBRARIES} Threads::Threads)
    add_test(NAME windows_alias_freeze COMMAND windows_alias_freeze_tests)
    set_tests_properties(windows_alias_freeze PROPERTIES TIMEOUT 30)
endif()

# Full native owners and software Vulkan; separate from portable unit tests so a missing
# graphics driver is an explicit CI failure rather than a silently skipped acceptance.
add_executable(controlled_cpu_scheduler_tests core/controlled_cpu_scheduler.cpp)
set_property(TARGET controlled_cpu_scheduler_tests PROPERTY UNITY_BUILD OFF)
target_link_libraries(controlled_cpu_scheduler_tests PRIVATE common core Vulkan::Headers
    ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)
add_test(NAME controlled_cpu_scheduler_tests COMMAND controlled_cpu_scheduler_tests)
set_tests_properties(controlled_cpu_scheduler_tests PROPERTIES TIMEOUT 180)

# Separate process: native Kernel TLS must never be reused across Systems. This
# owner acceptance uses real objects and an owned Null renderer, without Vulkan.
add_executable(restricted_process_owner_tests core/restricted_process_owner.cpp)
set_property(TARGET restricted_process_owner_tests PROPERTY UNITY_BUILD OFF)
target_link_libraries(restricted_process_owner_tests PRIVATE common core Vulkan::Headers
    ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)
add_test(NAME native_restricted_process_owner COMMAND restricted_process_owner_tests "[native-restricted-owner]")
set_tests_properties(native_restricted_process_owner PROPERTIES TIMEOUT 90)
add_test(NAME native_restricted_owner_unknown COMMAND restricted_process_owner_tests "[native-owner-unknown]")
add_test(NAME native_restricted_owner_unpolled COMMAND restricted_process_owner_tests "[native-owner-unpolled]")
set_tests_properties(native_restricted_owner_unknown native_restricted_owner_unpolled PROPERTIES TIMEOUT 90)
# A new System in the same host thread would reuse KernelCore's TLS dummy KThread
# from the first System. Keep both native owners intact and isolate their processes.
add_test(NAME native_f3_witness COMMAND controlled_cpu_scheduler_tests "[f3]")
set_tests_properties(native_f3_witness PROPERTIES TIMEOUT 30)
add_test(NAME native_f3_transfer COMMAND controlled_cpu_scheduler_tests "[f3-transfer]")
set_tests_properties(native_f3_transfer PROPERTIES TIMEOUT 30)
add_test(NAME native_f3_provenance COMMAND controlled_cpu_scheduler_tests "[f3-provenance]")
set_tests_properties(native_f3_provenance PROPERTIES TIMEOUT 30)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ARCHITECTURE_x86_64)
    add_test(NAME native_process_execution_stop
             COMMAND controlled_cpu_scheduler_tests "[process-execution-stop]")
    set_tests_properties(native_process_execution_stop PROPERTIES TIMEOUT 45)
    add_test(NAME native_process_execution_stop_cross_process
             COMMAND controlled_cpu_scheduler_tests "[process-execution-stop-cross-process]")
    set_tests_properties(native_process_execution_stop_cross_process PROPERTIES TIMEOUT 45)
    add_test(NAME native_process_cohort_stop
             COMMAND controlled_cpu_scheduler_tests "[process-cohort-stop]")
    set_tests_properties(native_process_cohort_stop PROPERTIES TIMEOUT 45)
    if (_pinned_container_compat)
        # The pinned unchecked LinkBlockFast loop has no halt check. Keep this
        # negative in its own process: its safe terminal boundary is OS exit.
        add_test(NAME native_process_execution_stop_fast_link_unknown
                 COMMAND controlled_cpu_scheduler_tests "[process-execution-stop-fast-link]")
        set_tests_properties(native_process_execution_stop_fast_link_unknown PROPERTIES TIMEOUT 15)
    endif()
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(tests PRIVATE precompiled_headers.h)
endif()
