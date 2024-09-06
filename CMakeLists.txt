cmake_minimum_required(VERSION 3.21)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# Determine version from the vcpkg json manifest
file(READ vcpkg.json VCPKG_MANIFEST)
string(JSON VCPKG_VERSION GET ${VCPKG_MANIFEST} "version")

project(toybox
        VERSION ${VCPKG_VERSION}
        DESCRIPTION "A personal game engine toybox project"
        LANGUAGES C CXX)

option(TB_BUILD_SAMPLES "Compile samples" ON)
option(TB_BUILD_VIEWER "Build the viewer application" ON)
option(TB_FINAL "Compile with the intention to redistribute" OFF)
option(TB_PROFILE_TRACY "Compile with support for the tracy profiler" ON)

# Include Helpers
include(${CMAKE_MODULE_PATH}/tb_app.cmake)
include(${CMAKE_MODULE_PATH}/tb_cooking.cmake)
include(${CMAKE_MODULE_PATH}/tb_packages.cmake)

# Generate config header
execute_process(COMMAND git log -1 --format=%h
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(tb_engine_config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
configure_file(source/tb_config.h.in ${tb_engine_config_include_dir}/tb_engine_config.h @ONLY)

# Setup toybox as an object library
file(GLOB_RECURSE lib_source CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.c" "${CMAKE_CURRENT_LIST_DIR}/source/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/source/*.h")
add_library(toybox OBJECT ${lib_source})

# Add default options
tb_options(toybox)

# Set include directories
target_include_directories(toybox PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(toybox PUBLIC "source/" "${CGLTF_INCLUDE_DIRS}" "${tb_engine_config_include_dir}")
# Mingw generates warnings from generated shader headers. Include as a system directory to ignore
target_include_directories(toybox SYSTEM PUBLIC "${CMAKE_CFG_INTDIR_ABS}/shaders")

# Link libraries
target_link_libraries(toybox PUBLIC ${tb_library_list})
if(TB_PROFILE_TRACY)
  target_link_libraries(toybox PUBLIC "Tracy::TracyClient")
endif()

if(TB_STATIC)
  target_link_libraries(toybox PUBLIC SDL3::SDL3-static)
else()
  target_link_libraries(toybox PUBLIC SDL3::SDL3)
endif()
target_link_libraries(toybox PUBLIC $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer>,SDL3_mixer::SDL3_mixer,SDL3_mixer::SDL3_mixer-static>)

# Cook engine shaders
tb_cook_shaders(tb_engine_shader_sources tb_engine_shader_headers)
add_custom_target(tb_engine_shaders ALL DEPENDS ${tb_engine_shader_headers})
target_sources(tb_engine_shaders PRIVATE ${tb_engine_shader_sources})
add_dependencies(toybox tb_engine_shaders)

# Cook engine assets
if(TB_COOK_ASSETS)
  tb_cook_assets(toybox engine_assets_path)
endif()

# Add optional sub-projects

if(TB_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if(TB_BUILD_VIEWER)
  add_subdirectory(viewer)
endif()

# Continue on to configure addons
include(${CMAKE_CURRENT_LIST_DIR}/addons/CMakeLists.txt)
