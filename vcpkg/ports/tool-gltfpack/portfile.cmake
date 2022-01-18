# This port represents a dependency on gltfpack
# In the future, it is expected that this port acquires and installs gltfpack.
# Currently is used in ports that call vcpkg_find_acquire_program(GLTFPACK) in order to force rebuilds.

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
vcpkg_find_acquire_program(GLTFPACK)
message(STATUS "Using gltfpack: ${GLTFPACK}")

get_filename_component(gltfpack_path ${GLTFPACK} DIRECTORY)

vcpkg_copy_tools(
    TOOL_NAMES gltfpack
    SEARCH_DIR ${gltfpack_path}
    DESTINATION ${CURRENT_PACKAGES_DIR}/tools/gltfpack/
)