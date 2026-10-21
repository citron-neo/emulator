# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
include(TargetArchitectureSpecificSources)

add_executable(dynarmic_tests
    fp/FPToFixed.cpp
    fp/FPValue.cpp
    fp/mantissa_util_tests.cpp
    fp/unpacked_tests.cpp
    rand_int.h
    # A32
    A32/test_arm_instructions.cpp
    A32/test_coprocessor.cpp
    A32/test_svc.cpp
    A32/test_thumb_instructions.cpp
    A32/testenv.h
    decoder_tests.cpp
    # A64
    A64/a64.cpp
    A64/fibonacci.cpp
    A64/fp_min_max.cpp
    A64/misaligned_page_table.cpp
    A64/test_invalidation.cpp
    A64/real_world.cpp
    A64/testenv.h
)
target_link_libraries(dynarmic_tests PRIVATE merry::oaknut unordered_dense::unordered_dense)

if (DYNARMIC_TESTS_USE_UNICORN)
    target_link_libraries(dynarmic_tests PRIVATE Unicorn::Unicorn)
    target_sources(dynarmic_tests PRIVATE
        fuzz_util.cpp
        fuzz_util.h
        # A32
        A32/fuzz_arm.cpp
        A32/fuzz_thumb.cpp
        unicorn_emu/a32_unicorn.cpp
        unicorn_emu/a32_unicorn.h
        # A64
        A64/fuzz_with_unicorn.cpp
        A64/verify_unicorn.cpp
        unicorn_emu/a64_unicorn.cpp
        unicorn_emu/a64_unicorn.h
    )
endif()

if ("riscv" IN_LIST ARCHITECTURE)
    target_link_libraries(dynarmic_tests PRIVATE biscuit::biscuit)
endif()

if ("x86_64" IN_LIST ARCHITECTURE)
    target_link_libraries(dynarmic_tests PRIVATE xbyak::xbyak)
    target_architecture_specific_sources(dynarmic_tests "x86_64"
        x64_cpu_info.cpp
        native/preserve_xmm.cpp
    )

    if (NOT MSVC AND NOT MULTIARCH_BUILD)
        target_sources(dynarmic_tests PRIVATE
            rsqrt_test.cpp
            rsqrt_test_fn.s
        )
    endif()
endif()

include(CreateDirectoryGroups)

#
# dynarmic_print_info
#
add_executable(dynarmic_print_info
    print_info.cpp
)
create_target_directory_groups(dynarmic_print_info)
target_link_libraries(dynarmic_print_info PRIVATE dynarmic fmt::fmt unordered_dense::unordered_dense)
if (BOOST_NO_HEADERS)
    target_link_libraries(dynarmic_print_info PRIVATE Boost::variant Boost::icl Boost::pool)
else()
    target_link_libraries(dynarmic_print_info PRIVATE Boost::headers)
endif()
target_include_directories(dynarmic_print_info PRIVATE . ../src)
target_compile_options(dynarmic_print_info PRIVATE ${DYNARMIC_CXX_FLAGS})
target_compile_definitions(dynarmic_print_info PRIVATE FMT_USE_USER_DEFINED_LITERALS=1)

#
# dynarmic_test_generator
#
add_executable(dynarmic_test_generator
    fuzz_util.cpp
    fuzz_util.h
    test_generator.cpp
)

create_target_directory_groups(dynarmic_test_generator)

target_link_libraries(dynarmic_test_generator PRIVATE dynarmic fmt::fmt unordered_dense::unordered_dense)
if (BOOST_NO_HEADERS)
    target_link_libraries(dynarmic_test_generator PRIVATE Boost::variant Boost::icl Boost::pool)
else()
    target_link_libraries(dynarmic_test_generator PRIVATE Boost::headers)
endif()
target_include_directories(dynarmic_test_generator PRIVATE . ../src)
target_compile_options(dynarmic_test_generator PRIVATE ${DYNARMIC_CXX_FLAGS})
target_compile_definitions(dynarmic_test_generator PRIVATE FMT_USE_USER_DEFINED_LITERALS=1)

#
# dynarmic_test_reader
#
add_executable(dynarmic_test_reader
    test_reader.cpp
)
create_target_directory_groups(dynarmic_test_reader)
target_link_libraries(dynarmic_test_reader PRIVATE dynarmic fmt::fmt unordered_dense::unordered_dense)
if (BOOST_NO_HEADERS)
    target_link_libraries(dynarmic_test_reader PRIVATE Boost::variant Boost::icl Boost::pool)
else()
    target_link_libraries(dynarmic_test_reader PRIVATE Boost::headers)
endif()
target_include_directories(dynarmic_test_reader PRIVATE . ../src)
target_compile_options(dynarmic_test_reader PRIVATE ${DYNARMIC_CXX_FLAGS})
target_compile_definitions(dynarmic_test_reader PRIVATE FMT_USE_USER_DEFINED_LITERALS=1)

#
create_target_directory_groups(dynarmic_tests)

target_link_libraries(dynarmic_tests PRIVATE dynarmic Catch2::Catch2WithMain fmt::fmt unordered_dense::unordered_dense)
if (BOOST_NO_HEADERS)
    target_link_libraries(dynarmic_tests PRIVATE Boost::variant Boost::icl Boost::pool)
else()
    target_link_libraries(dynarmic_tests PRIVATE Boost::headers)
endif()
target_include_directories(dynarmic_tests PRIVATE . ../src)
target_compile_options(dynarmic_tests PRIVATE ${DYNARMIC_CXX_FLAGS})
target_compile_definitions(dynarmic_tests PRIVATE FMT_USE_USER_DEFINED_LITERALS=1)

add_test(NAME dynarmic_tests COMMAND dynarmic_tests --durations yes)
