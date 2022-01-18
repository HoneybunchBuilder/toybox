# header-only library
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KhronosGroup/Vulkan-Headers
    REF v1.2.176
    SHA512 8b2744679dfe32aadceba6d489b29476ac3a8ad5c20df181bee6606f72ed6580f4a6220b8c9a2170df7ebe0ce73bd5b0ca4cc8467e4b1643fcb74f2322d05173
    HEAD_REF master
)

# This must be vulkan as other vulkan packages expect it there.
file(COPY "${SOURCE_PATH}/include/vulkan/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/vulkan")

# Handle copyright
file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
