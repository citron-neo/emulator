# SPDX-FileCopyrightText: 2023 Alexandre Bouvier <contact@amb.tf>
#
# SPDX-License-Identifier: GPL-3.0-or-later

find_path(RenderDoc_INCLUDE_DIR renderdoc_app.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RenderDoc
    REQUIRED_VARS RenderDoc_INCLUDE_DIR
)

if (RenderDoc_FOUND AND NOT TARGET RenderDoc::API)
    add_library(RenderDoc::API INTERFACE IMPORTED)
    set_target_properties(RenderDoc::API PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RenderDoc_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(RenderDoc_INCLUDE_DIR)
