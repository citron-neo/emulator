# The pinned vcpkg Oboe port installs a static library and headers without
# exporting a CMake config target. Keep its layout confined to this adapter.
if (NOT TARGET oboe::oboe)
    add_library(oboe::oboe STATIC IMPORTED GLOBAL)
    set_target_properties(oboe::oboe PROPERTIES
        IMPORTED_LOCATION "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/liboboe.a"
        INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
endif()
