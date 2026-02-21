# CopyMinGWDeps.cmake
# Recursively resolves and copies all MinGW DLL dependencies for a target executable.
# Usage: copy_mingw_deps(target_name)

function(copy_mingw_deps target)
    # Find objdump for dependency scanning
    find_program(OBJDUMP_EXECUTABLE objdump)
    if (NOT OBJDUMP_EXECUTABLE)
        message(WARNING "objdump not found, cannot auto-deploy MinGW DLLs")
        return()
    endif()

    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    # Write out the deploy script that runs at build time
    set(DEPLOY_SCRIPT "${CMAKE_BINARY_DIR}/deploy_mingw_deps.cmake")
    file(WRITE "${DEPLOY_SCRIPT}" "
# Auto-generated MinGW DLL deployment script
set(OBJDUMP \"${OBJDUMP_EXECUTABLE}\")
set(MINGW_BIN \"${MINGW_BIN_DIR}\")
set(EXE_DIR \"\${TARGET_DIR}\")

# Recursively collect all DLL dependencies from the MinGW prefix
function(resolve_deps file visited_var deps_var)
    execute_process(
        COMMAND \${OBJDUMP} -p \"\${file}\"
        OUTPUT_VARIABLE objdump_out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCHALL \"DLL Name: [^\r\n]+\" dll_entries \"\${objdump_out}\")
    foreach(entry \${dll_entries})
        string(REGEX REPLACE \"DLL Name: +\" \"\" dll_name \"\${entry}\")
        string(STRIP \"\${dll_name}\" dll_name)
        # Skip if already visited
        list(FIND \${visited_var} \"\${dll_name}\" idx)
        if (NOT idx EQUAL -1)
            continue()
        endif()
        # Only copy DLLs that exist in the MinGW bin directory
        set(dll_path \"\${MINGW_BIN}/\${dll_name}\")
        if (EXISTS \"\${dll_path}\")
            list(APPEND \${visited_var} \"\${dll_name}\")
            list(APPEND \${deps_var} \"\${dll_path}\")
            # Recurse into this DLL's dependencies
            resolve_deps(\"\${dll_path}\" \${visited_var} \${deps_var})
        endif()
    endforeach()
    set(\${visited_var} \${\${visited_var}} PARENT_SCOPE)
    set(\${deps_var} \${\${deps_var}} PARENT_SCOPE)
endfunction()

set(visited \"\")
set(all_deps \"\")
resolve_deps(\"\${EXE_DIR}/citron.exe\" visited all_deps)

list(LENGTH all_deps dep_count)
message(STATUS \"Deploying \${dep_count} MinGW DLL(s) to \${EXE_DIR}\")
foreach(dll \${all_deps})
    get_filename_component(dll_name \"\${dll}\" NAME)
    if (NOT EXISTS \"\${EXE_DIR}/\${dll_name}\")
        file(COPY \"\${dll}\" DESTINATION \"\${EXE_DIR}\")
        message(STATUS \"  Copied: \${dll_name}\")
    endif()
endforeach()

# Deploy Qt6 plugins
set(QT_PLUGIN_BASE \"\${MINGW_BIN}/../share/qt6/plugins\")
if (EXISTS \"\${QT_PLUGIN_BASE}\")
    foreach(plugin_dir platforms styles imageformats)
        if (EXISTS \"\${QT_PLUGIN_BASE}/\${plugin_dir}\")
            file(MAKE_DIRECTORY \"\${EXE_DIR}/\${plugin_dir}\")
            file(GLOB plugin_dlls \"\${QT_PLUGIN_BASE}/\${plugin_dir}/*.dll\")
            foreach(plugin \${plugin_dlls})
                file(COPY \"\${plugin}\" DESTINATION \"\${EXE_DIR}/\${plugin_dir}\")
            endforeach()
            list(LENGTH plugin_dlls pcount)
            message(STATUS \"  Copied \${pcount} plugin(s) to \${plugin_dir}/\")
        endif()
    endforeach()
endif()
")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DTARGET_DIR="$<TARGET_FILE_DIR:${target}>" -P "${DEPLOY_SCRIPT}"
        COMMENT "Deploying MinGW runtime DLLs for ${target}"
    )
endfunction()
