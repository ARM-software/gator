vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO michaelrsweet/mxml
    REF v3.0
    SHA512 3c540348022226b122ce014f183368cda6631600a11d8f4d43c99c62fba2f3ca4b23dff4122082f63915b59a46a5475d1d12d0081f9315c65f5c044ac3c751d6
    HEAD_REF master
    PATCHES
        SDDAP-11022.patch
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})
file(COPY ${CMAKE_CURRENT_LIST_DIR}/config.h DESTINATION ${SOURCE_PATH})

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/include)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/share)

file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
