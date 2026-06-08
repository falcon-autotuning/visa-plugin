vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 60cf1ed7cdd86a27c279696855273e13b6bdb19bc72bb1e74c4db698dbed35a4e6fcf7fa8df488208c2282324c53652735d13990a3ef3aed1576975e09e0cfad
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
