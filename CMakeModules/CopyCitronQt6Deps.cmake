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
    elseif(MINGW)
        # For MinGW builds with CITRON_USE_BUNDLED_QT=ON the bundled Qt tree is
        # at build/externals/qt/<ver>/mingw_64/.  CopyMinGWDeps.cmake handles the
        # runtime DLLs; this function handles the plugin subdirectories that
        # windeployqt would normally deploy but which are not on the DLL search path.

        # Resolve the plugin root from Qt6_DIR (which points to lib/cmake/Qt6).
        set(Qt6_BUNDLED_PLUGINS "${Qt6_DIR}/../../../plugins")

        foreach(plugin_dir platforms styles imageformats iconengines)
            if (EXISTS "${Qt6_BUNDLED_PLUGINS}/${plugin_dir}")
                add_custom_command(TARGET ${target_dir} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_DEST}${plugin_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${Qt6_BUNDLED_PLUGINS}/${plugin_dir}"
                        "${DLL_DEST}${plugin_dir}"
                    COMMENT "Copying Qt6 ${plugin_dir} plugins (MinGW bundled)"
                )
            endif()
        endforeach()

        # TLS plugins — critical for HTTPS/SSL.
        if (EXISTS "${Qt6_BUNDLED_PLUGINS}/tls")
            add_custom_command(TARGET ${target_dir} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${TLS}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${Qt6_BUNDLED_PLUGINS}/tls"
                    "${TLS}"
                COMMENT "Copying Qt6 TLS plugins (MinGW bundled)"
            )
        else()
            message(WARNING "Qt6 TLS plugin directory not found at ${Qt6_BUNDLED_PLUGINS}/tls")
            message(WARNING "SSL/HTTPS will not work in this build.")
        endif()
    endif()

    # Create an empty qt.conf so Qt locates its plugins relative to the executable.
    add_custom_command(TARGET citron POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch ${DLL_DEST}qt.conf
    )
endfunction(copy_citron_Qt6_deps)
