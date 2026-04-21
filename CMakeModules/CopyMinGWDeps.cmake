# CopyMinGWDeps.cmake
# Recursively resolves and copies all MinGW DLL dependencies for a target executable.
# Also deploys Qt6 plugins including TLS backends required for SSL/HTTPS.
# Usage: copy_mingw_deps(target_name)

function(copy_mingw_deps target)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SEARCH_PATHS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Prefer llvm-readobj for robust PE parsing on both Linux and Windows hosts.
    # We look for it in the same directory as the compiler.
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

    # Define search paths for MinGW DLLs.
    # We prioritize explicitly provided paths (FFmpeg, Qt) over the compiler's bin directory
    # so that bundled/minimal libraries are found before fat system ones.
    set(MINGW_SEARCH_PATHS "")
    if (ARG_SEARCH_PATHS)
        list(APPEND MINGW_SEARCH_PATHS ${ARG_SEARCH_PATHS})
    endif()

    # Automatically add Qt target bin path if defined
    if (QT_TARGET_PATH AND EXISTS "${QT_TARGET_PATH}/bin")
        list(APPEND MINGW_SEARCH_PATHS "${QT_TARGET_PATH}/bin")
    elseif (Qt6_DIR AND EXISTS "${Qt6_DIR}/../../../bin")
        get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
        list(APPEND MINGW_SEARCH_PATHS "${QT_BIN_DIR}")
    endif()

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
set(SEARCH_PATHS \"${MINGW_SEARCH_PATHS}\")
set(EXE_DIR \"\${TARGET_DIR}\")
set(TARGET_FILE \"\${TARGET_FILE}\")
set(COMPILER_BIN_DIR \"${COMPILER_BIN_DIR}\")

# 1. Deploy Qt6 plugins (Targeted scan to avoid hanging on large 'user' data folder)
# We use a strict whitelist based on the cross-compiled build reference to avoid pulling in 
# heavy transitive dependencies like Glib/GTK/GIO (often triggered by obscure image formats).
set(QT_PLATFORMS_PLUGINS qdirect2d qminimal qoffscreen qwindows)
set(QT_STYLES_PLUGINS qmodernwindowsstyle qwindowsvistastyle)
set(QT_IMAGEFORMATS_PLUGINS qgif qicns qico qjpeg qsvg qtga qtiff qwbmp qwebp)
set(QT_ICONENGINES_PLUGINS qsvgicon)
set(QT_TLS_PLUGINS qcertonlybackend qopensslbackend qschannelbackend)

set(QT_PLUGIN_BASE \"\${COMPILER_BIN_DIR}/../share/qt6/plugins\")
if (NOT EXISTS \"\${QT_PLUGIN_BASE}\")
    set(QT_PLUGIN_BASE \"${Qt6_DIR}/../../../plugins\")
endif()

if (EXISTS \"\${QT_PLUGIN_BASE}\")
    set(PLUGIN_SUBDIRS platforms styles imageformats iconengines tls)
    foreach(subdir \${PLUGIN_SUBDIRS})
        if (EXISTS \"\${QT_PLUGIN_BASE}/\${subdir}\")
            file(MAKE_DIRECTORY \"\${EXE_DIR}/\${subdir}\")
            
            # Use the defined whitelist for this subdirectory
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
        
        # Search for the DLL in all provided search paths
        set(dll_path \"\${dll_name}-NOTFOUND\")
        foreach(search_path \${SEARCH_PATHS})
            if (EXISTS \"\${search_path}/\${dll_name}\")
                set(dll_path \"\${search_path}/\${dll_name}\")
                break()
            endif()
        endforeach()

        if (EXISTS \"\${dll_path}\" AND NOT IS_DIRECTORY \"\${dll_path}\")
            list(APPEND \${visited_var} \"\${dll_name_lower}\")
            list(APPEND \${deps_var} \"\${dll_path}\")
            
            # Diagnostic status message to verify which path was chosen (bundled vs system)
            message(STATUS \"  Found \${dll_name} in: \${dll_path}\")
            
            resolve_deps(\"\${dll_path}\" \${visited_var} \${deps_var})
        elseif (NOT \"\${dll_name_lower}\" MATCHES \"^(advapi32|kernel32|user32|gdi32|shell32|ole32|oleaut32|ws2_32|comdlg32|mpr|winmm|version|imm32|msvcrt|comctl32|shlwapi|crypt32|bcrypt|ntdll|dbghelp|setupapi|iphlpapi|winhttp|wininet|dwmapi|uxtheme|dnsapi|cryptui|wintrust|cfgmgr32|powrprof|onecore|hid|dsound|msvcp_win|winspool|api-ms-win-|ext-ms-win-).*\\.dll$\")
            message(STATUS \"  Warning: Could not find DLL: \${dll_name}\")
        endif()
    endforeach()
    set(\${visited_var} \${\${visited_var}} PARENT_SCOPE)
    set(\${deps_var} \${\${deps_var}} PARENT_SCOPE)
endfunction()

set(visited \"\")
set(all_deps \"\")

# Resolve for the targeted executable
resolve_deps(\"\${EXE_DIR}/\${TARGET_FILE}\" visited all_deps)

# Recursively resolve only for the explicitly whitelisted plugins we deployed above.
# We skip a global recursive scan of the output directory to avoid pulling in unneeded 
# dependencies from \"other\" DLLs that might have been present in the bin folder.
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
