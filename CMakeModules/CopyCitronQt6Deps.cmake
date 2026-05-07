# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

function(copy_citron_Qt6_deps target_dir)
    include(WindowsCopyFiles)
    if (MSVC)
        set(DLL_DEST "$<TARGET_FILE_DIR:${target_dir}>/")
        set(Qt6_DLL_DIR "${Qt6_DIR}/../../../bin")
    else()
        set(DLL_DEST "${CMAKE_BINARY_DIR}/bin/")
        set(Qt6_DLL_DIR "${Qt6_DIR}/../../../lib/")
    endif()
    set(Qt6_PLATFORMS_DIR "${Qt6_DIR}/../../../plugins/platforms/")
    set(Qt6_STYLES_DIR "${Qt6_DIR}/../../../plugins/styles/")
    set(Qt6_IMAGEFORMATS_DIR "${Qt6_DIR}/../../../plugins/imageformats/")
    set(Qt6_ICONENGINES_DIR "${Qt6_DIR}/../../../plugins/iconengines/")
    set(Qt6_TLS_DIR "${Qt6_DIR}/../../../plugins/tls/")
    set(Qt6_RESOURCES_DIR "${Qt6_DIR}/../../../resources/")
    set(PLATFORMS ${DLL_DEST}platforms/)
    set(STYLES ${DLL_DEST}styles/)
    set(IMAGEFORMATS ${DLL_DEST}imageformats/)
    set(ICONENGINES ${DLL_DEST}iconengines/)
    # Qt 6 loads TLS backends from a "tls/" subdirectory next to the executable.
    # Without these plugins QNetworkAccessManager cannot establish SSL connections.
    set(TLS ${DLL_DEST}tls/)

    if (MSVC)
        windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
            Qt6Core$<$<CONFIG:Debug>:d>.*
            Qt6Gui$<$<CONFIG:Debug>:d>.*
            Qt6Widgets$<$<CONFIG:Debug>:d>.*
            Qt6Network$<$<CONFIG:Debug>:d>.*
            Qt6Svg$<$<CONFIG:Debug>:d>.*
        )
        if (CITRON_USE_QT_MULTIMEDIA)
            windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
                Qt6Multimedia$<$<CONFIG:Debug>:d>.*
            )
        endif()
        if (CITRON_USE_QT_WEB_ENGINE)
            windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
                Qt6WebEngineCore$<$<CONFIG:Debug>:d>.*
                Qt6WebEngineWidgets$<$<CONFIG:Debug>:d>.*
                QtWebEngineProcess$<$<CONFIG:Debug>:d>.*
            )
            windows_copy_files(${target_dir} ${Qt6_RESOURCES_DIR} ${DLL_DEST}
                icudtl.dat
                qtwebengine_devtools_resources.pak
                qtwebengine_resources.pak
                qtwebengine_resources_100p.pak
                qtwebengine_resources_200p.pak
            )
        endif()
        windows_copy_files(citron ${Qt6_PLATFORMS_DIR} ${PLATFORMS} qwindows$<$<CONFIG:Debug>:d>.*)
        windows_copy_files(citron ${Qt6_STYLES_DIR} ${STYLES} qwindowsvistastyle$<$<CONFIG:Debug>:d>.*)
        windows_copy_files(citron ${Qt6_IMAGEFORMATS_DIR} ${IMAGEFORMATS}
            qjpeg$<$<CONFIG:Debug>:d>.*
            qgif$<$<CONFIG:Debug>:d>.*
            qpng$<$<CONFIG:Debug>:d>.*
            qsvg$<$<CONFIG:Debug>:d>.*
        )
        windows_copy_files(citron ${Qt6_ICONENGINES_DIR} ${ICONENGINES}
            qsvgicon$<$<CONFIG:Debug>:d>.*
        )
        # TLS plugins for SSL/HTTPS support (required for web service / auto updater).
        windows_copy_files(citron ${Qt6_TLS_DIR} ${TLS}
            qschannelbackend$<$<CONFIG:Debug>:d>.*
            qopensslbackend$<$<CONFIG:Debug>:d>.*
        )
    elseif(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # For MinGW and Native Linux builds with CITRON_USE_BUNDLED_QT=ON,
        # copy the plugins and libraries to subdirectories to keep bin/ clean.
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(LIB_DEST "${DLL_DEST}lib/")
            set(PLUGINS_DEST "${DLL_DEST}plugins/")
            set(USE_LIB_FOLDER ON)
        else()
            # Windows/MinGW: DLLs must be next to the EXE
            set(LIB_DEST "${DLL_DEST}")
            set(PLUGINS_DEST "${DLL_DEST}")
            set(USE_LIB_FOLDER OFF)
        endif()

        set(Qt6_BUNDLED_PLUGINS "${Qt6_DIR}/../../../plugins")

        foreach(plugin_dir platforms styles imageformats iconengines tls wayland-graphics-integration wayland-shell-integration)
            if (EXISTS "${Qt6_BUNDLED_PLUGINS}/${plugin_dir}")
                add_custom_command(TARGET ${target_dir} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINS_DEST}${plugin_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${Qt6_BUNDLED_PLUGINS}/${plugin_dir}"
                        "${PLUGINS_DEST}${plugin_dir}"
                    COMMENT "Bundling Qt6 ${plugin_dir} plugins"
                )
            endif()
        endforeach()

        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # For Linux, also copy the shared libraries.
            set(_qt_libs Core Gui Widgets Network Svg OpenGL DBus WaylandClient WaylandEglClient WaylandEglClientHwIntegration XcbQpa)
            if (CITRON_USE_QT_MULTIMEDIA)
                list(APPEND _qt_libs Multimedia MultimediaWidgets)
            endif()

            foreach(_lib ${_qt_libs})
                if (EXISTS "${Qt6_DLL_DIR}libQt6${_lib}.so.6")
                    add_custom_command(TARGET ${target_dir} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${Qt6_DLL_DIR}libQt6${_lib}.so.6"
                            "${LIB_DEST}"
                        COMMENT "Bundling libQt6${_lib}.so.6"
                    )
                endif()
            endforeach()

            find_program(PATCHELF_EXE patchelf)
            if (NOT PATCHELF_EXE)
                message(FATAL_ERROR
                    "patchelf is required for portable Linux CPM bundles. "
                    "Run ./build-citron-linux.sh setup or install patchelf with your package manager.")
            endif()

            # Bundle ICU libraries (required by QtCore)
            if (DEFINED ICU_BINARY_DIR)
                # If we built it ourselves, we know what we expect.
                # Note: We can't use GLOB at configuration time if it's a fresh build.
                set(_icu_libs data i18n io test tu uc)
                foreach(_icu_comp ${_icu_libs})
                    set(_icu_lib "${ICU_BINARY_DIR}/libicu${_icu_comp}.so.73")
                    add_custom_command(TARGET ${target_dir} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icu_lib}" "${LIB_DEST}"
                        COMMENT "Bundling ICU library: ${_icu_lib}"
                    )
                endforeach()
            else()
                file(GLOB _icu_libs "${Qt6_DLL_DIR}libicu*.so.[0-9]*")
                foreach(_icu_lib ${_icu_libs})
                    add_custom_command(TARGET ${target_dir} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icu_lib}" "${LIB_DEST}"
                        COMMENT "Bundling ICU library: ${_icu_lib}"
                    )
                endforeach()
            endif()

            # Bundle additional XCB support libraries
            set(_xcb_deps 
                xcb.so.1 Xau.so.6 xcb-xkb.so.1
                xcb-cursor.so.0 xcb-icccm.so.4 xcb-image.so.0 xcb-keysyms.so.1
                xcb-util.so.1
                xcb-render-util.so.0 xcb-xinerama.so.0 xcb-xinput.so.0
                xcb-shm.so.0 xcb-render.so.0 xcb-randr.so.0 xcb-shape.so.0
                xcb-xfixes.so.0 xcb-sync.so.1 xcb-dri3.so.0
                # Additional system libs needed by Qt XCB plugin
                xkbcommon.so.0 xkbcommon-x11.so.0 X11-xcb.so.1 Xdmcp.so.6
            )
            
            if (DEFINED XCB_BINARY_DIR)
                set(_xcb_search_dir "${XCB_BINARY_DIR}/")
            else()
                set(_xcb_search_dir "SYSTEM")
            endif()

            foreach(_xcb_lib ${_xcb_deps})
                if (_xcb_search_dir STREQUAL "SYSTEM" OR _xcb_lib MATCHES "xkbcommon|X11-xcb")
                    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=lib${_xcb_lib} OUTPUT_VARIABLE _LIB_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
                    if (NOT EXISTS "${_LIB_PATH}")
                        # Fallback for some systems where print-file-name doesn't work well
                        set(_LIB_PATH "/usr/lib/x86_64-linux-gnu/lib${_xcb_lib}")
                    endif()
                else()
                    set(_LIB_PATH "${_xcb_search_dir}lib${_xcb_lib}")
                endif()

                # Always add the bundling command for built libs, or if system lib exists
                add_custom_command(TARGET ${target_dir} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_LIB_PATH}" "${LIB_DEST}"
                    COMMENT "Bundling XCB support library: ${_xcb_lib}"
                )
            endforeach()

            # Bundle libstdc++ and libgcc_s
            execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libstdc++.so.6 OUTPUT_VARIABLE _STDCXX_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
            execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgcc_s.so.1 OUTPUT_VARIABLE _GCC_S_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
            
            if (EXISTS "${_STDCXX_PATH}")
                add_custom_command(TARGET ${target_dir} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_STDCXX_PATH}" "${LIB_DEST}"
                    COMMENT "Bundling libstdc++.so.6"
                )
            endif()
            if (EXISTS "${_GCC_S_PATH}")
                add_custom_command(TARGET ${target_dir} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DEST}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_GCC_S_PATH}" "${LIB_DEST}"
                    COMMENT "Bundling libgcc_s.so.1"
                )
            endif()

            set(_runtime_dependency_dirs "${LIB_DEST}" "${Qt6_DLL_DIR}")
            if (DEFINED ICU_BINARY_DIR)
                list(APPEND _runtime_dependency_dirs "${ICU_BINARY_DIR}")
            endif()
            if (DEFINED XCB_BINARY_DIR)
                list(APPEND _runtime_dependency_dirs "${XCB_BINARY_DIR}")
            endif()
            list(REMOVE_DUPLICATES _runtime_dependency_dirs)
            string(REPLACE ";" "|" _runtime_dependency_dirs_arg "${_runtime_dependency_dirs}")

            add_custom_command(TARGET ${target_dir} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DBUNDLE_BIN_DIR="${DLL_DEST}"
                    -DPATCHELF_EXE="${PATCHELF_EXE}"
                    -DRUNTIME_DEPENDENCY_DIRS="${_runtime_dependency_dirs_arg}"
                    -P "${CMAKE_SOURCE_DIR}/CMakeModules/FixLinuxBundleRpaths.cmake"
                COMMENT "Copying Linux runtime dependencies and normalizing RPATHs"
            )
        endif()
    endif()

    # Create a qt.conf next to the executable.
    file(MAKE_DIRECTORY "${DLL_DEST}")
    if (USE_LIB_FOLDER)
        file(WRITE "${DLL_DEST}qt.conf" "[Paths]\nPlugins = plugins\n")
    else()
        file(WRITE "${DLL_DEST}qt.conf" "")
    endif()
endfunction(copy_citron_Qt6_deps)
