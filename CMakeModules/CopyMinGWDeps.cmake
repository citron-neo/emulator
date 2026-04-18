# CopyMinGWDeps.cmake
# Recursively resolves and copies all MinGW DLL dependencies for a target executable.
# Also deploys Qt6 plugins including TLS backends required for SSL/HTTPS.
# Usage: copy_mingw_deps(target_name)

function(copy_mingw_deps target)
    find_program(OBJDUMP_EXECUTABLE objdump)
    if (NOT OBJDUMP_EXECUTABLE)
        message(WARNING "objdump not found, cannot auto-deploy MinGW DLLs")
        return()
    endif()

    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    set(DEPLOY_SCRIPT "${CMAKE_BINARY_DIR}/deploy_mingw_deps.cmake")
    file(WRITE "${DEPLOY_SCRIPT}" "
# Auto-generated MinGW DLL deployment script
set(OBJDUMP \"${OBJDUMP_EXECUTABLE}\")
set(MINGW_BIN \"${MINGW_BIN_DIR}\")
set(EXE_DIR \"\${TARGET_DIR}\")

# Recursively collect all DLL dependencies from the MinGW prefix.
# NOTE: We always overwrite existing DLLs (no NOT EXISTS guard) so that a
# restored build cache never leaves stale DLLs from an older MSYS2 snapshot.
# Stale Qt DLLs cause \"no Qt platform plugin\" errors at startup because
# citron.exe is compiled against the current Qt headers but links the old DLLs.
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
        list(FIND \${visited_var} \"\${dll_name}\" idx)
        if (NOT idx EQUAL -1)
            continue()
        endif()
        set(dll_path \"\${MINGW_BIN}/\${dll_name}\")
        if (EXISTS \"\${dll_path}\")
            list(APPEND \${visited_var} \"\${dll_name}\")
            list(APPEND \${deps_var} \"\${dll_path}\")
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
    # Always overwrite — do NOT use NOT EXISTS here.
    file(COPY \"\${dll}\" DESTINATION \"\${EXE_DIR}\")
    message(STATUS \"  Deployed: \${dll_name}\")
endforeach()

# Deploy Qt6 plugins from the MSYS2 UCRT64 Qt installation.
# CopyMinGWDeps handles the Qt DLL binaries; this block handles the plugins.
# windeployqt6 is unreliable when WINDEPLOYQT_EXECUTABLE is not found by cmake,
# so we copy the plugin directories directly from the Qt share tree.
set(QT_PLUGIN_BASE \"\${MINGW_BIN}/../share/qt6/plugins\")
# On a Linux cross-compile host the toolchain bin dir has no Qt share tree.
# Fall back to the path derived from Qt6_DIR (resolved at configure time).
# Qt6_DIR points to <qt_root>/lib/cmake/Qt6, so ../../.. is <qt_root>.
if (NOT EXISTS \\"\\${QT_PLUGIN_BASE}\\")
    set(QT_PLUGIN_BASE \\"${Qt6_DIR}/../../../plugins\\")
    if (EXISTS \\"\\${QT_PLUGIN_BASE}\\")
        message(STATUS \\"Qt plugins: aqt install at \\${QT_PLUGIN_BASE}\\")
    else()
        set(QT_PLUGIN_BASE \\"\\")
    endif()
endif()
if (EXISTS \"\${QT_PLUGIN_BASE}\")
    # Core platform / style / imageformat plugins
    foreach(plugin_dir platforms styles imageformats)
        if (EXISTS \"\${QT_PLUGIN_BASE}/\${plugin_dir}\")
            file(MAKE_DIRECTORY \"\${EXE_DIR}/\${plugin_dir}\")
            file(GLOB plugin_dlls \"\${QT_PLUGIN_BASE}/\${plugin_dir}/*.dll\")
            foreach(plugin \${plugin_dlls})
                file(COPY \"\${plugin}\" DESTINATION \"\${EXE_DIR}/\${plugin_dir}\")
            endforeach()
            list(LENGTH plugin_dlls pcount)
            message(STATUS \"  Deployed \${pcount} plugin(s) to \${plugin_dir}/\")
        endif()
    endforeach()

    # TLS plugins — required for Qt network SSL/HTTPS (OpenSSL + Schannel backends).
    # Without these, QNetworkAccessManager silently fails all HTTPS requests and
    # web service features are broken. Qt 6 no longer bundles TLS statically;
    # it loads these at runtime via the plugin system.
    if (EXISTS \"\${QT_PLUGIN_BASE}/tls\")
        file(MAKE_DIRECTORY \"\${EXE_DIR}/tls\")
        file(GLOB tls_dlls \"\${QT_PLUGIN_BASE}/tls/*.dll\")
        foreach(plugin \${tls_dlls})
            file(COPY \"\${plugin}\" DESTINATION \"\${EXE_DIR}/tls\")
        endforeach()
        list(LENGTH tls_dlls tcount)
        message(STATUS \"  Deployed \${tcount} TLS plugin(s) to tls/\")
    else()
        message(WARNING \"Qt TLS plugin directory not found at \${QT_PLUGIN_BASE}/tls\")
        message(WARNING \"HTTPS/SSL will not work. Install mingw-w64-ucrt-x86_64-qt6-base.\")
    endif()
else()
    message(WARNING \"Qt plugin base not found at \${QT_PLUGIN_BASE}\")
    message(WARNING \"Qt plugins (platforms, TLS, imageformats) will not be deployed.\")
endif()
")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DTARGET_DIR="$<TARGET_FILE_DIR:${target}>" -P "${DEPLOY_SCRIPT}"
        COMMENT "Deploying MinGW runtime DLLs and Qt plugins for ${target}"
    )
endfunction()
