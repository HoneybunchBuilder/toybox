cmake_minimum_required(VERSION 3.10)

project(Tracy LANGUAGES CXX)

# TODO: Maybe pull this from a header?
set(TRACY_VERSION_MAJOR 0)
set(TRACY_VERSION_MINOR 7)
set(TRACY_VERSION_PATCH 8)

set(TRACY_VERSION "${TRACY_VERSION_MAJOR}.${TRACY_VERSION_MINOR}.${TRACY_VERSION_PATCH}")

find_package(Threads REQUIRED)

add_library(TracyClient TracyClient.cpp)
add_library(Tracy::TracyClient ALIAS TracyClient)

set_target_properties(TracyClient PROPERTIES
    VERSION ${TRACY_VERSION}
)

target_compile_features(TracyClient PUBLIC cxx_std_11)
target_include_directories(TracyClient PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:include>
)
target_link_libraries(
    TracyClient
    PRIVATE
        Threads::Threads
)
if(NOT SWITCH)
    target_link_libraries(
        TracyClient
        PUBLIC
            ${CMAKE_DL_LIBS}
    )
endif()

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    find_library(EXECINFO_LIBRARY NAMES execinfo REQUIRED)
    target_link_libraries(TracyClient PUBLIC ${EXECINFO_LIBRARY})
endif()

macro(set_option option help value scope)
    option(${option} ${help} ${value})
    if(${option})
        message(STATUS "${option}: ON")
		target_compile_definitions(TracyClient ${scope} ${option})
    else()
        message(STATUS "${option}: OFF")
    endif()
endmacro()

# Added PRIVATE scope so TRACY_ENABLE doesn't propagate to vcpkg
set_option(TRACY_ENABLE "Enable profiling" ON PRIVATE)
set_option(TRACY_ON_DEMAND "On-demand profiling" OFF PUBLIC)
set_option(TRACY_CALLSTACK "Collect call stacks" OFF PUBLIC)
set_option(TRACY_ONLY_LOCALHOST "Only listen on the localhost interface" OFF PUBLIC)
set_option(TRACY_NO_BROADCAST "Disable client discovery by broadcast to local network" OFF PUBLIC)
set_option(TRACY_NO_CODE_TRANSFER "Disable collection of source code" OFF PUBLIC)
set_option(TRACY_NO_CONTEXT_SWITCH "Disable capture of context switches" OFF PUBLIC)
set_option(TRACY_NO_EXIT "Client executable does not exit until all profile data is sent to server" OFF PUBLIC)
set_option(TRACY_NO_FRAME_IMAGE "Disable capture of frame images" OFF PUBLIC)
set_option(TRACE_NO_SAMPLING "Disable call stack sampling" OFF PUBLIC)
set_option(TRACY_NO_VERIFY "Disable zone validation for C API" OFF PUBLIC)
set_option(TRACY_NO_VSYNC_CAPTURE "Disable capture of hardware Vsync events" OFF PUBLIC)

if (BUILD_SHARED_LIBS)
	target_compile_definitions(TracyClient PRIVATE TRACY_EXPORTS)
	target_compile_definitions(TracyClient INTERFACE TRACY_IMPORTS)
endif()

set(INSTALL_TARGETS TracyClient)
set(TRACY_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/tracy)

install(TARGETS ${INSTALL_TARGETS} EXPORT tracyConfig
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

export(TARGETS ${INSTALL_TARGETS}
    NAMESPACE Tracy::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/tracyConfig.cmake"
)

install(EXPORT
    tracyConfig DESTINATION ${TRACY_CMAKE_DIR}
    NAMESPACE Tracy::
)

# TODO: Figure out how to get this to work using a pattern
install(FILES
    Tracy.hpp
	TracyC.h
	TracyD3D11.hpp
	TracyD3D12.hpp
	TracyLua.hpp
	TracyOpenCL.hpp
	TracyOpenGL.hpp
	TracyVulkan.hpp
    DESTINATION include
)

install(
	DIRECTORY common/
	DESTINATION include/common
	FILES_MATCHING
	PATTERN "*.h"
	PATTERN "*.hpp"
)

install(
	DIRECTORY client/
	DESTINATION include/client
	FILES_MATCHING
	PATTERN "*.h"
	PATTERN "*.hpp"
)