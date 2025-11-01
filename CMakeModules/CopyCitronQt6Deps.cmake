# SPDX-FileCopyrightText: 2025 citron Emulator Project
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
    set(Qt6_TLS_DIR "${Qt6_DIR}/../../../plugins/tls/")
    set(Qt6_RESOURCES_DIR "${Qt6_DIR}/../../../resources/")
    set(PLATFORMS ${DLL_DEST}plugins/platforms/)
    set(STYLES ${DLL_DEST}plugins/styles/)
    set(IMAGEFORMATS ${DLL_DEST}plugins/imageformats/)
    set(TLS ${DLL_DEST}tls/)

    if (MSVC)
        windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
            Qt6Core$<$<CONFIG:Debug>:d>.*
            Qt6Gui$<$<CONFIG:Debug>:d>.*
            Qt6Widgets$<$<CONFIG:Debug>:d>.*
            Qt6Network$<$<CONFIG:Debug>:d>.*
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
        )
        # Copy TLS plugins for SSL/HTTPS support (required for auto updater)
        windows_copy_files(citron ${Qt6_TLS_DIR} ${TLS}
            qschannelbackend$<$<CONFIG:Debug>:d>.*
            qopensslbackend$<$<CONFIG:Debug>:d>.*
        )
    else()
        # Update for non-MSVC platforms if needed
    endif()

    # Create an empty qt.conf file
    add_custom_command(TARGET citron POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch ${DLL_DEST}qt.conf
    )
endfunction(copy_citron_Qt6_deps)
