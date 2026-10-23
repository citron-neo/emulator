cmake_minimum_required(VERSION 3.12)
project(dynarmic LANGUAGES C CXX ASM VERSION 6.7.0)

# Determine if we're built as a subproject (using add_subdirectory)
# or if this is the master project.
set(MASTER_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(MASTER_PROJECT ON)
endif()

if (MASTER_PROJECT)
    include(CTest)
endif()

# Dynarmic project options
option(DYNARMIC_ENABLE_CPU_FEATURE_DETECTION "Turning this off causes dynarmic to assume the host CPU doesn't support anything later than SSE3" ON)
option(DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT "Enables support for systems that require W^X" OFF)
option(DYNARMIC_FATAL_ERRORS "Errors are fatal" OFF)
option(DYNARMIC_IGNORE_ASSERTS "Ignore asserts" OFF)
option(DYNARMIC_TESTS "Build tests" ${BUILD_TESTING})
option(DYNARMIC_TESTS_USE_UNICORN "Enable fuzzing tests against unicorn" OFF)
option(DYNARMIC_USE_LLVM "Support disassembly of jitted x86_64 code using LLVM" OFF)
option(DYNARMIC_USE_PRECOMPILED_HEADERS "Use precompiled headers" ON)
option(DYNARMIC_USE_BUNDLED_EXTERNALS "Use all bundled externals (useful when e.g. cross-compiling)" OFF)
option(DYNARMIC_WARNINGS_AS_ERRORS "Warnings as errors" ${MASTER_PROJECT})
if (NOT DEFINED DYNARMIC_FRONTENDS)
    set(DYNARMIC_FRONTENDS "A32;A64" CACHE STRING "Selects which frontends to enable")
endif()

# Default to a Release build
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    message(STATUS "Defaulting to a Release build")
endif()

# Set hard requirements for C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable in-source builds
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(SEND_ERROR "In-source builds are not allowed.")
endif()

# Add the module directory to the list of paths
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/CMakeModules")

# Compiler flags
if (MSVC)
    set(DYNARMIC_CXX_FLAGS
        /experimental:external
        /external:W0
        /external:anglebrackets
        /W4
        /w44263 # Non-virtual member function hides base class virtual function
        /w44265 # Class has virtual functions, but destructor is not virtual
        /w44456 # Declaration of 'var' hides previous local declaration
        /w44457 # Declaration of 'var' hides function parameter
        /w44458 # Declaration of 'var' hides class member
        /w44459 # Declaration of 'var' hides global definition
        /w44946 # Reinterpret-cast between related types
        /wd4592 # Symbol will be dynamically initialized (implementation limitation)
        /permissive- # Stricter C++ standards conformance
        /MP
        /Zi
        /Zo
        /EHsc
        /Zc:externConstexpr # Allows external linkage for variables declared "extern constexpr", as the standard permits.
        /Zc:inline          # Omits inline functions from object-file output.
        /Zc:throwingNew     # Assumes new (without std::nothrow) never returns null.
        /volatile:iso       # Use strict standard-abiding volatile semantics
        /bigobj             # Increase number of sections in .obj files
        /DNOMINMAX)

    if (DYNARMIC_WARNINGS_AS_ERRORS)
        list(APPEND DYNARMIC_CXX_FLAGS
             /WX)
    endif()

    if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
        list(APPEND DYNARMIC_CXX_FLAGS
             -Qunused-arguments
             -Wno-missing-braces)
    endif()
else()
    set(DYNARMIC_CXX_FLAGS
        -Wall
        -Wextra
        -Wcast-qual
        -pedantic
        -Wno-missing-braces)

    if (DYNARMIC_WARNINGS_AS_ERRORS)
        list(APPEND DYNARMIC_CXX_FLAGS
             -pedantic-errors
             -Werror)
    endif()

    if (DYNARMIC_FATAL_ERRORS)
        list(APPEND DYNARMIC_CXX_FLAGS
             -Wfatal-errors)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # GCC produces bogus -Warray-bounds warnings from xbyak headers for code paths that are not
        # actually reachable.  Specifically, it happens in cases where some code casts an Operand&
        # to Address& after first checking isMEM(), and that code is inlined in a situation where
        # GCC knows that the variable is actually a Reg64.  isMEM() will never return true for a
        # Reg64, but GCC doesn't know that.
        list(APPEND DYNARMIC_CXX_FLAGS -Wno-array-bounds)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
        # Bracket depth determines maximum size of a fold expression in Clang since 9c9974c3ccb6.
        # And this in turns limits the size of a std::array.
        list(APPEND DYNARMIC_CXX_FLAGS -fbracket-depth=1024)
    endif()
endif()

# Arch detection
include(DetectArchitecture)

# Forced use of individual bundled libraries for non-REQUIRED library is possible with e.g. cmake -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON ...

if (DYNARMIC_USE_BUNDLED_EXTERNALS)
    set(CMAKE_DISABLE_FIND_PACKAGE_biscuit ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_Catch2 ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_fmt ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_oaknut ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_unordered_dense ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_xbyak ON)
endif()

find_package(Boost 1.57 REQUIRED)
find_package(fmt 9 CONFIG)
find_package(unordered_dense CONFIG)

if ("arm64" IN_LIST ARCHITECTURE OR DYNARMIC_TESTS)
    find_package(oaknut 2.0.1 CONFIG)
endif()

if ("riscv" IN_LIST ARCHITECTURE)
    find_package(biscuit 0.9.1 QUIET)
endif()

if ("x86_64" IN_LIST ARCHITECTURE)
    find_package(xbyak 7 CONFIG)
endif()

if (DYNARMIC_USE_LLVM)
    find_package(LLVM REQUIRED)
    separate_arguments(LLVM_DEFINITIONS)
endif()

if (DYNARMIC_TESTS)
    find_package(Catch2 3 CONFIG)
    if (DYNARMIC_TESTS_USE_UNICORN)
        find_package(Unicorn REQUIRED)
    endif()
endif()

# Pull in externals CMakeLists for libs where available
add_subdirectory(externals)

# Dynarmic project files
add_subdirectory(src/dynarmic)
if (DYNARMIC_TESTS)
    add_subdirectory(tests)
endif()
