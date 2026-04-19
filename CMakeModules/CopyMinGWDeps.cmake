# CopyMinGWDeps.cmake
# Recursively resolves and copies all MinGW DLL dependencies for a target executable.
# Also deploys Qt6 plugins including TLS backends required for SSL/HTTPS.
# Usage: copy_mingw_deps(target_name)

function(copy_mingw_deps target)
    set(options \"\")
    set(oneValueArgs \"\")
    set(multiValueArgs SEARCH_PATHS)
    cmake_parse_arguments(ARG \"\${options}\" \"\${oneValueArgs}\" \"\${multiValueArgs}\" \${ARGN})

    # Prefer llvm-readobj for robust PE parsing on both Linux and Windows hosts.
    # We look for it in the same directory as the compiler.
    get_filename_component(COMPILER_BIN_DIR \"\${CMAKE_CXX_COMPILER}\" DIRECTORY)
    find_program(READOBJ_EXECUTABLE NAMES llvm-readobj llvm-readobj-19 llvm-readobj-18 llvm-readobj-17
                 HINTS \"\${COMPILER_BIN_DIR}\")
    
    if (READOBJ_EXECUTABLE)
        set(DUMP_TOOL \"\${READOBJ_EXECUTABLE}\")
        set(DUMP_MODE \"READOBJ\")
    else()
        find_program(OBJDUMP_EXECUTABLE NAMES objdump x86_64-w64-mingw32-objdump
                     HINTS \"\${COMPILER_BIN_DIR}\")
        if (OBJDUMP_EXECUTABLE)
            set(DUMP_TOOL \"\${OBJDUMP_EXECUTABLE}\")
            set(DUMP_MODE \"OBJDUMP\")
        else()
            message(WARNING \"Neither llvm-readobj nor objdump found. MinGW DLL deployment may fail.\")
            return()
        endif()
    endif()

    # Define search paths for MinGW DLLs.
    # 1. The compiler's bin directory (standard for MSYS2).
    # 2. The sysroot's bin directory (standard for llvm-mingw cross-compilation).
    # 3. Any additional paths provided in SEARCH_PATHS (e.g. Qt, FFmpeg).
    set(MINGW_SEARCH_PATHS \"\${COMPILER_BIN_DIR}\")
    if (CMAKE_CROSSCOMPILING AND CMAKE_FIND_ROOT_PATH)
        list(APPEND MINGW_SEARCH_PATHS \"\${CMAKE_FIND_ROOT_PATH}/bin\")
    endif()
    
    if (ARG_SEARCH_PATHS)
        list(APPEND MINGW_SEARCH_PATHS \${ARG_SEARCH_PATHS})
    endif()

    # Automatically add Qt target bin path if defined
    if (QT_TARGET_PATH AND EXISTS \"\${QT_TARGET_PATH}/bin\")
        list(APPEND MINGW_SEARCH_PATHS \"\${QT_TARGET_PATH}/bin\")
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
set(QT_PLUGIN_BASE \"\${COMPILER_BIN_DIR}/../share/qt6/plugins\")
if (NOT EXISTS \"\${QT_PLUGIN_BASE}\")
    set(QT_PLUGIN_BASE \"${Qt6_DIR}/../../../plugins\")
    if (NOT EXISTS \"\${QT_PLUGIN_BASE}\")
        set(QT_PLUGIN_BASE \"\")
    endif()
endif()

if (EXISTS \"\${QT_PLUGIN_BASE}\")
    set(PLUGIN_SUBDIRS platforms styles imageformats iconengines tls)
    foreach(subdir \${PLUGIN_SUBDIRS})
        if (EXISTS \"\${QT_PLUGIN_BASE}/\${subdir}\")
            file(MAKE_DIRECTORY \"\${EXE_DIR}/\${subdir}\")
            file(GLOB plugin_dlls \"\${QT_PLUGIN_BASE}/\${subdir}/*.dll\")
            foreach(plugin \${plugin_dlls})
                file(COPY \"\${plugin}\" DESTINATION \"\${EXE_DIR}/\${subdir}\")
            endforeach()
            list(LENGTH plugin_dlls pcount)
            if (pcount GREATER 0)
                message(STATUS \"  Deployed \${pcount} plugin(s) to \${subdir}/\")
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
            resolve_deps(\"\${dll_path}\" \${visited_var} \${deps_var})
        endif()
    endforeach()
    set(\${visited_var} \${\${visited_var}} PARENT_SCOPE)
    set(\${deps_var} \${\${deps_var}} PARENT_SCOPE)
endfunction()

set(visited \"\")
set(all_deps \"\")

# Resolve for the targeted executable
resolve_deps(\"\${EXE_DIR}/\${TARGET_FILE}\" visited all_deps)

# Resolve for all deployed DLLs in the output directory (plugins, etc.)
file(GLOB_RECURSE deployed_dlls \"\${EXE_DIR}/*/*.dll\")
foreach(dll \${deployed_dlls})
    resolve_deps(\"\${dll}\" visited all_deps)
endforeach()

# 3. Deploy everything
if (all_deps)
    list(REMOVE_DUPLICATES all_deps)
    list(LENGTH all_deps dep_count)
    message(STATUS \"Deploying \${dep_count} MinGW DLL(s) to \${EXE_DIR}\")
    
    set(files_to_copy \"\")
    foreach(dll \${all_deps})
        get_filename_component(name \"\${dll}\" NAME)
        if (NOT EXISTS \"\${EXE_DIR}/\${name}\")
            list(APPEND files_to_copy \"\${dll}\")
        endif()
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
