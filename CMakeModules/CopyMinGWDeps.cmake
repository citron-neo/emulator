# CopyMinGWDeps.cmake
# Recursively resolves and copies all MinGW DLL dependencies for a target executable.
# Also deploys Qt6 plugins including TLS backends required for SSL/HTTPS.
# Usage: copy_mingw_deps(target_name)

function(copy_mingw_deps target)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SEARCH_PATHS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Prefer llvm-readobj from the compiler toolchain.
    get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(READOBJ_EXECUTABLE NAMES llvm-readobj llvm-readobj-19 llvm-readobj-18 llvm-readobj-17
                 HINTS "${COMPILER_BIN_DIR}")
    
    if (READOBJ_EXECUTABLE)
        set(DUMP_TOOL "${READOBJ_EXECUTABLE}")
        set(DUMP_MODE "READOBJ")
    else()
        find_program(OBJDUMP_EXECUTABLE NAMES objdump x86_64-w64-mingw32-objdump
                     HINTS "${COMPILER_BIN_DIR}")
        if (OBJDUMP_EXECUTABLE)
            set(DUMP_TOOL "${OBJDUMP_EXECUTABLE}")
            set(DUMP_MODE "OBJDUMP")
        else()
            message(WARNING \"Neither llvm-readobj nor objdump found. MinGW DLL deployment may fail.\")
            return()
        endif()
    endif()

    # Prefer bundled DLL locations before the toolchain bin dir.
    set(MINGW_SEARCH_PATHS "")
    if (ARG_SEARCH_PATHS)
        list(APPEND MINGW_SEARCH_PATHS ${ARG_SEARCH_PATHS})
    endif()

    # Add Qt runtime bin if available.
    if (QT_TARGET_PATH AND EXISTS "${QT_TARGET_PATH}/bin")
        list(APPEND MINGW_SEARCH_PATHS "${QT_TARGET_PATH}/bin")
    elseif (Qt6_DIR AND EXISTS "${Qt6_DIR}/../../../bin")
        get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
        list(APPEND MINGW_SEARCH_PATHS "${QT_BIN_DIR}")
    endif()

    set(TOOLCHAIN_RUNTIME_PATHS "${COMPILER_BIN_DIR}")
    get_filename_component(COMPILER_PREFIX_DIR "${COMPILER_BIN_DIR}" DIRECTORY)
    get_filename_component(COMPILER_NAME "${CMAKE_CXX_COMPILER}" NAME)
    string(REGEX REPLACE "-clang\\+\\+$" "" COMPILER_TRIPLE "${COMPILER_NAME}")
    if (COMPILER_TRIPLE AND EXISTS "${COMPILER_PREFIX_DIR}/${COMPILER_TRIPLE}/bin")
        list(APPEND TOOLCHAIN_RUNTIME_PATHS "${COMPILER_PREFIX_DIR}/${COMPILER_TRIPLE}/bin")
    endif()
    if (CMAKE_CROSSCOMPILING AND CMAKE_FIND_ROOT_PATH)
        foreach(root_path IN LISTS CMAKE_FIND_ROOT_PATH)
            if (EXISTS "${root_path}/bin")
                list(APPEND TOOLCHAIN_RUNTIME_PATHS "${root_path}/bin")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES TOOLCHAIN_RUNTIME_PATHS)

    list(APPEND MINGW_SEARCH_PATHS "${COMPILER_BIN_DIR}")
    if (CMAKE_CROSSCOMPILING AND CMAKE_FIND_ROOT_PATH)
        list(APPEND MINGW_SEARCH_PATHS "${CMAKE_FIND_ROOT_PATH}/bin")
    endif()
    
    list(REMOVE_DUPLICATES MINGW_SEARCH_PATHS)

    set(DEPLOY_SCRIPT "${CMAKE_BINARY_DIR}/deploy_mingw_deps_${target}.cmake")
    file(WRITE "${DEPLOY_SCRIPT}" "
# Auto-generated MinGW DLL deployment script
set(DUMP_TOOL \"${DUMP_TOOL}\")
set(DUMP_MODE \"${DUMP_MODE}\")
# Search local bin first.
set(SEARCH_PATHS \"\${EXE_DIR};${MINGW_SEARCH_PATHS}\")
set(EXE_DIR \"\${TARGET_DIR}\")
set(TARGET_FILE \"\${TARGET_FILE}\")
set(COMPILER_BIN_DIR \"${COMPILER_BIN_DIR}\")
set(TOOLCHAIN_RUNTIME_PATHS \"${TOOLCHAIN_RUNTIME_PATHS}\")
set(QT_TARGET_PLUGIN_DIR \"${QT_TARGET_PATH}/plugins\")

# 1. Deploy whitelisted Qt6 plugins.
set(QT_PLATFORMS_PLUGINS qdirect2d qminimal qoffscreen qwindows)
set(QT_STYLES_PLUGINS qmodernwindowsstyle qwindowsvistastyle)
set(QT_IMAGEFORMATS_PLUGINS qgif qicns qico qjpeg qsvg qtga qtiff qwbmp qwebp)
set(QT_ICONENGINES_PLUGINS qsvgicon)
set(QT_TLS_PLUGINS qcertonlybackend qopensslbackend qschannelbackend)
set(TOOLCHAIN_RUNTIME_DLLS
    libc++.dll
    libunwind.dll)
set(ALLOWED_COMPILER_BIN_DLLS
    libbrotlicommon.dll
    libbrotlidec.dll
    libenet-7.dll
    libiconv-2.dll
    libopus-0.dll
    libspeexdsp-1.dll
    libva.dll
    libva_win32.dll
    zlib1.dll)

set(QT_PLUGIN_BASE \"\${QT_TARGET_PLUGIN_DIR}\")
if (NOT EXISTS \"\${QT_PLUGIN_BASE}\")
    set(QT_PLUGIN_BASE \"${Qt6_DIR}/../../../plugins\")
endif()
if (NOT EXISTS \"\${QT_PLUGIN_BASE}\")
    set(QT_PLUGIN_BASE \"\${COMPILER_BIN_DIR}/../share/qt6/plugins\")
endif()

if (EXISTS \"\${QT_PLUGIN_BASE}\")
    set(PLUGIN_SUBDIRS platforms styles imageformats iconengines tls)
    foreach(subdir \${PLUGIN_SUBDIRS})
        if (EXISTS \"\${QT_PLUGIN_BASE}/\${subdir}\")
            file(MAKE_DIRECTORY \"\${EXE_DIR}/\${subdir}\")
            
            # Use the matching whitelist for this plugin dir.
            string(TOUPPER \"\${subdir}\" subdir_upper)
            set(whitelist \${QT_\${subdir_upper}_PLUGINS})
            
            set(pcount 0)
            foreach(plugin_name \${whitelist})
                set(plugin_path \"\${QT_PLUGIN_BASE}/\${subdir}/\${plugin_name}.dll\")
                if (EXISTS \"\${plugin_path}\")
                    file(COPY \"\${plugin_path}\" DESTINATION \"\${EXE_DIR}/\${subdir}\")
                    math(EXPR pcount \"\${pcount} + 1\")
                endif()
            endforeach()

            if (pcount GREATER 0)
                message(STATUS \"  Deployed \${pcount} whitelist plugin(s) to \${subdir}/\")
            endif()
        endif()
    endforeach()
endif()

file(WRITE \"\${EXE_DIR}/qt.conf\" \"[Paths]\nPlugins = .\n\")

# 2. Recursively collect all DLL dependencies
function(resolve_deps file visited_var deps_var)
    if (DUMP_MODE STREQUAL \"READOBJ\")
        execute_process(
            COMMAND \${DUMP_TOOL} --coff-imports \"\${file}\"
            OUTPUT_VARIABLE dump_out
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCHALL \"Name: [^\\r\\n]+\" dll_entries \"\${dump_out}\")
        set(REGEX_REPLACE \"Name: +\")
    else()
        execute_process(
            COMMAND \${DUMP_TOOL} -p \"\${file}\"
            OUTPUT_VARIABLE dump_out
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCHALL \"DLL Name: [^\\r\\n]+\" dll_entries \"\${dump_out}\")
        set(REGEX_REPLACE \"DLL Name: +\")
    endif()

    foreach(entry \${dll_entries})
        string(REGEX REPLACE \"\${REGEX_REPLACE}\" \"\" dll_name \"\${entry}\")
        string(STRIP \"\${dll_name}\" dll_name)
        string(REGEX REPLACE \"[^ -~]\" \"\" dll_name \"\${dll_name}\")
        string(TOLOWER \"\${dll_name}\" dll_name_lower)
        list(FIND \${visited_var} \"\${dll_name_lower}\" idx)
        if (NOT idx EQUAL -1)
            continue()
        endif()
        
        # Search all candidate paths in order.
        set(dll_path \"\${dll_name}-NOTFOUND\")
        foreach(search_path \${SEARCH_PATHS})
            # Skip host MSYS2 DLLs except the allowed leaves and toolchain runtimes.
            if (\"\${search_path}\" MATCHES \"([/\\\\])msys64([/\\\\])\")
                list(FIND TOOLCHAIN_RUNTIME_DLLS \"\${dll_name_lower}\" toolchain_runtime_idx)
                if (NOT toolchain_runtime_idx EQUAL -1)
                    # Keep Clang runtimes from the active toolchain.
                else()
                list(FIND ALLOWED_COMPILER_BIN_DLLS \"\${dll_name_lower}\" compiler_bin_allow_idx)
                if (compiler_bin_allow_idx EQUAL -1)
                    continue()
                endif()
                endif()
            endif()
            
            if (EXISTS \"\${search_path}/\${dll_name}\")
                set(dll_path \"\${search_path}/\${dll_name}\")
                break()
            endif()
        endforeach()

        if (EXISTS \"\${dll_path}\" AND NOT IS_DIRECTORY \"\${dll_path}\")
            list(APPEND \${visited_var} \"\${dll_name_lower}\")
            list(APPEND \${deps_var} \"\${dll_path}\")
            
            # Log the chosen path for the DLL.
            message(STATUS \"  Found \${dll_name} in: \${dll_path}\")
            
            resolve_deps(\"\${dll_path}\" \${visited_var} \${deps_var})
        elseif (NOT \"\${dll_name_lower}\" MATCHES \"^(advapi32|authz|avrt|bcrypt|cfgmgr32|comctl32|comdlg32|crypt32|cryptui|d2d1|d3d9|d3d11|d3d12|dbghelp|dnsapi|dsound|dwmapi|dxgi|dwrite|ext-ms-win-|gdi32|hid|imm32|iphlpapi|kernel32|mpr|msvcp_win|mswsock|ncrypt|netapi32|ntdll|ole32|oleaut32|onecore|powrprof|secur32|setupapi|shcore|shell32|shlwapi|user32|userenv|uxtheme|version|winhttp|wininet|winmm|winspool|wintrust|wtsapi32|ws2_32|api-ms-win-).*[.]dll$\")
            message(STATUS \"  Warning: Could not find DLL: \${dll_name}\")
        endif()
    endforeach()
    set(\${visited_var} \${\${visited_var}} PARENT_SCOPE)
    set(\${deps_var} \${\${deps_var}} PARENT_SCOPE)
endfunction()

set(visited \"\")
set(all_deps \"\")

# 2.3 Pin core Clang runtimes first.
foreach(runtime libc++.dll libunwind.dll libgcc_s_seh-1.dll libstdc++-6.dll)
    set(runtime_search_paths \"\${TOOLCHAIN_RUNTIME_PATHS};\${SEARCH_PATHS}\")
    foreach(search_path \${runtime_search_paths})
        if (EXISTS \"\${search_path}/\${runtime}\")
            string(TOLOWER \"\${runtime}\" runtime_lower)
            list(FIND visited \"\${runtime_lower}\" runtime_idx)
            if (runtime_idx EQUAL -1)
                list(APPEND visited \"\${runtime_lower}\")
                list(APPEND all_deps \"\${search_path}/\${runtime}\")
                message(STATUS \"  Pinned \${runtime} from: \${search_path}/\${runtime}\")
            endif()
            resolve_deps(\"\${search_path}/\${runtime}\" visited all_deps)
            break()
        endif()
    endforeach()
endforeach()

# Resolve for the targeted executable
resolve_deps(\"\${EXE_DIR}/\${TARGET_FILE}\" visited all_deps)

# Resolve only the plugins we deployed above.
foreach(subdir platforms styles imageformats iconengines tls)
    if (EXISTS \"\${EXE_DIR}/\${subdir}\")
        file(GLOB plugin_dlls \"\${EXE_DIR}/\${subdir}/*.dll\")
        foreach(dll \${plugin_dlls})
            resolve_deps(\"\${dll}\" visited all_deps)
        endforeach()
    endif()
endforeach()

# 3. Deploy everything
if (all_deps)
    list(REMOVE_DUPLICATES all_deps)
    list(LENGTH all_deps dep_count)
    message(STATUS \"Deploying \${dep_count} MinGW DLL(s) to \${EXE_DIR}\")
    
    set(files_to_copy \"\")
    foreach(dll \${all_deps})
        list(APPEND files_to_copy \"\${dll}\")
    endforeach()
    
    if (files_to_copy)
        list(LENGTH files_to_copy copy_count)
        message(STATUS \"  Copying \${copy_count} missing DLL(s)...\")
        foreach(f \${files_to_copy})
            get_filename_component(fn \"\${f}\" NAME)
            message(STATUS \"    -> \${fn}\")
            file(COPY \"\${f}\" DESTINATION \"\${EXE_DIR}\")
        endforeach()
        message(STATUS \"  Deployment complete.\")
    else()
        message(STATUS \"  All DLLs are already up to date.\")
    endif()
endif()
")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DTARGET_DIR="$<TARGET_FILE_DIR:${target}>" -DTARGET_FILE="$<TARGET_FILE_NAME:${target}>" -P "${DEPLOY_SCRIPT}"
        COMMENT "Deploying MinGW runtime DLLs and Qt plugins for ${target}"
    )
endfunction()
