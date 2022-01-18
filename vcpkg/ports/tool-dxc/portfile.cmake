# This port represents a dependency on the DXC shader compiler
# In the future, it is expected that this port acquires and installs DXC.
# Currently is used in ports that call vcpkg_find_acquire_program(DXC) in order to force rebuilds.

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
vcpkg_find_acquire_program(DXC)
message(STATUS "Using DXC: ${DXC}")

get_filename_component(dxc_path ${DXC} DIRECTORY)

vcpkg_copy_tools(
    TOOL_NAMES dxc
    SEARCH_DIR ${dxc_path}
    DESTINATION ${CURRENT_PACKAGES_DIR}/tools/dxc/
)

file(COPY ${dxc_path}/dxcompiler.dll DESTINATION ${CURRENT_PACKAGES_DIR}/tools/dxc/)
file(COPY ${dxc_path}/dxil.dll DESTINATION ${CURRENT_PACKAGES_DIR}/tools/dxc/)