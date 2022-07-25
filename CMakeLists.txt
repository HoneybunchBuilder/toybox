cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED ENV{VCPKG_ROOT})
  message(FATAL_ERROR "Need VCPKG_ROOT environment variable defined")
endif()
set(VCPKG_ROOT $ENV{VCPKG_ROOT})

message(STATUS "Using vcpkg root ${VCPKG_ROOT}")

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
    CACHE STRING "Vcpkg toolchain file")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# TODO: Determine version from the vcpkg json manifest

project(toybox
        VERSION 0.1.0
        DESCRIPTION "A personal game engine toybox project"
        LANGUAGES C CXX)

option(PROFILE_TRACY "Compile with support for the tracy profiler" ON)

set(GAME_NAME "toybox")
set(GAME_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(GAME_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(GAME_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})

set(ENGINE_NAME "toybox")
set(ENGINE_VERSION_MAJOR 0)
set(ENGINE_VERSION_MINOR 1)
set(ENGINE_VERSION_PATCH 0)

set(config $<CONFIG>)

# Get target CPU architecture
include(${CMAKE_CURRENT_LIST_DIR}/cmake/TargetArch.cmake)
target_architecture(arch)

# Platform detection
if(WIN32)
  set(platform "windows")
elseif(APPLE)
  if(IOS)
    set(platform "ios")
  else()
    set(platform "macos")
  endif()
elseif(ANDROID)
  set(platform "android")
elseif(UNIX)
  set(platform "linux")
else()
  message(FATAL_ERROR "Unknown platform")
endif()

# Host detection
if(CMAKE_HOST_WIN32)
  set(host "windows")
elseif(CMAKE_HOST_UNIX)
  if(CMAKE_HOST_APPLE)
    set(host "osx")
  else()
    set(host "linux")
  endif()
else()
message(FATAL_ERROR "Unknown host")
endif()

if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(host_arch "x64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(host_arch "x64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
  set(host_arch "arm64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(host_arch "arm64")
else()
  message(FATAL_ERROR "Unknown host arch: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

set(host_triple ${host_arch}-${host})

# Set install prefix if not already overridden
set(toybox_install "${CMAKE_CURRENT_SOURCE_DIR}/install/${platform}")
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX ${toybox_install} CACHE PATH "Default Install Path" FORCE )
else()
    set(toybox_install ${CMAKE_INSTALL_PREFIX})
endif()

# Clang & GNU only for now
if(NOT ${CMAKE_C_COMPILER_ID} MATCHES "Clang" AND
   NOT ${CMAKE_C_COMPILER_ID} MATCHES "AppleClang" AND
   NOT ${CMAKE_C_COMPILER_ID} MATCHES "GNU")
  message(FATAL_ERROR "${CMAKE_C_COMPILER_ID} Unsupported - Clang & GNU only")
endif()

if(${VCPKG_TARGET_TRIPLET} MATCHES static OR ${VCPKG_TARGET_TRIPLET} MATCHES android)
  set(STATIC ON)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(sdl2-image CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Threads REQUIRED)
if(PROFILE_TRACY)
  find_package(Tracy CONFIG REQUIRED)
endif()
if(UNIX)
  find_package(Threads REQUIRED)
  if(NOT ANDROID)
    add_library(zstd::zstd ALIAS zstd::libzstd_static)
  endif()
endif()

find_path(CGLTF_INCLUDE_DIRS "cgltf.h")

set(VULKAN_BIN_PATH "bin")
if(platform MATCHES "windows")
  set(VULKAN_BIN_PATH "Bin")
endif()
# Prefer whatever dxc is in the vulkan SDK
# If this picks up a windows kit dxc it may not have spirv support
# but on linux a dxc on the default path will probably have spirv codegen 
# so *shrug*
find_program(DXC dxc
  PATHS $ENV{VULKAN_SDK}/${VULKAN_BIN_PATH}
  REQUIRED)
find_program(TOKTX toktx
  PATHS ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${host_triple}/tools/ktx
  REQUIRED)

# Compile Shaders
file(GLOB shader_includes "${CMAKE_CURRENT_LIST_DIR}/src/*.hlsli")
file(GLOB shader_files "${CMAKE_CURRENT_LIST_DIR}/src/*.hlsl")

foreach(shader ${shader_files})
  get_filename_component(filename ${shader} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(vert_out_path "${shader_out_path}/${filename}_vert.h")
  set(frag_out_path "${shader_out_path}/${filename}_frag.h")
  set(out_paths "${vert_out_path};${frag_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T vs_6_1 -E vert -Vn ${filename}_vert $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${vert_out_path}
      COMMAND ${DXC} -T ps_6_1 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${frag_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

file(GLOB rt_shader_files "${CMAKE_CURRENT_LIST_DIR}/src/*.hlsl.rt")
foreach(shader ${rt_shader_files})
  get_filename_component(filename ${shader} NAME_WLE)
  get_filename_component(filename ${filename} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(raygen_out_path "${shader_out_path}/${filename}_raygen.h")
  set(miss_out_path "${shader_out_path}/${filename}_miss.h")
  set(closehit_out_path "${shader_out_path}/${filename}_closehit.h")
  set(out_paths "${raygen_out_path};${miss_out_path};${closehit_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T lib_6_4 -E raygen -Vn ${filename}_raygen $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${raygen_out_path}
      COMMAND ${DXC} -T lib_6_4 -E miss -Vn ${filename}_miss $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${miss_out_path}
      COMMAND ${DXC} -T lib_6_4 -E closehit -Vn ${filename}_closehit $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${closehit_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

add_custom_target(shaders ALL DEPENDS ${shader_headers})

# Setup Main Executable
file(GLOB source "${CMAKE_CURRENT_LIST_DIR}/src/*.c" "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/src/*.h")

if(WIN32)
  if(MINGW)
    add_executable(toybox ${source} "${CMAKE_CURRENT_LIST_DIR}/src/res/toybox.rc")
    target_link_libraries(toybox PRIVATE mingw32)
    target_link_options(toybox PRIVATE -mwindows)
  else()
    add_executable(toybox WIN32 ${source} "${CMAKE_CURRENT_LIST_DIR}/src/res/toybox.rc")
    if(NOT MSVC)
      target_link_options(toybox PRIVATE -Wl,/subsystem:windows)
    endif()
  endif()

  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT toybox)
  set_property(TARGET toybox PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

  # This really only matters for clang-cl
  # ${CMAKE_C_COMPILER_ID} being MSVC would fail the above check
  # So thing of this more as if(MSVC-style-compiler)
  if(MSVC)
    target_compile_options(toybox PRIVATE -Wno-reserved-id-macro)
    target_compile_options(toybox PRIVATE -Wno-documentation)
  endif()

elseif(ANDROID)
  add_library(toybox SHARED ${source})
  if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "arm")
    target_link_libraries(toybox PRIVATE KTX::astcenc-neon-static)
  elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "x86")
    target_link_libraries(toybox PRIVATE KTX::astcenc-avx2-static)
  endif()
elseif(UNIX)
  if(APPLE)
    if(IOS)
      # iOS
      find_library(IMAGEIO ImageIO REQUIRED)
      find_library(CORESERVICES CoreServices REQUIRED)
      add_executable(toybox ${source} "${CMAKE_CURRENT_LIST_DIR}/linux/src/entry.c")
      target_link_libraries(toybox PRIVATE ${IMAGEIO} ${CORESERVICES})
      if(CMAKE_GENERATOR MATCHES "Xcode")
        set_target_properties(toybox PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.honeybunch.toybox")
      endif()
    else()
      # macOS
      add_executable(toybox ${source} "${CMAKE_CURRENT_LIST_DIR}/linux/src/entry.c")  
    endif()
  else()
    add_executable(toybox ${source} "${CMAKE_CURRENT_LIST_DIR}/linux/src/entry.c")
  endif()
endif()

# Disable clang warning
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(toybox PRIVATE -Wno-reserved-identifier)
endif()

add_dependencies(toybox shaders)

target_sources(toybox PRIVATE ${rt_shader_files} ${shader_files} ${shader_headers})

if(isMultiConfig)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()
#set_property(TARGET toybox PROPERTY INTERPROCEDURAL_OPTIMIZATION True)

set(library_list "SDL2::SDL2main;SDL2::SDL2_image;volk::volk;volk::volk_headers;imgui::imgui;mimalloc;mimalloc-static;KTX::ktx")

target_link_libraries(toybox PRIVATE ${library_list})

if(PROFILE_TRACY)
  target_link_libraries(toybox PRIVATE "Tracy::TracyClient")
endif()

# Get the latest abbreviated commit hash of the working branch
execute_process(COMMAND git log -1 --format=%h
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE)

# Generate config header
set(config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
configure_file(src/config.h.in ${config_include_dir}/config.h @ONLY)

target_include_directories(toybox PRIVATE "src/" "${CGLTF_INCLUDE_DIRS}" "${config_include_dir}")
# Mingw generates warnings from generated shader headers. Include as a system directory to ignore
target_include_directories(toybox SYSTEM PRIVATE "${CMAKE_CFG_INTDIR_ABS}/shaders")
target_compile_definitions(toybox PRIVATE "-DTB_CONFIG=\"$<CONFIG>\"")

if(STATIC)
  target_link_libraries(toybox PRIVATE SDL2::SDL2-static)
  set_property(TARGET toybox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_link_libraries(toybox PRIVATE SDL2::SDL2)
endif()

target_compile_features(toybox PRIVATE c_std_11)

# Turn on warnings
target_compile_options(toybox PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic> 
)

set(assets_dest "assets")
if(ANDROID)
  set(assets_dest "$<CONFIG>/assets")
  install(TARGETS toybox
          RUNTIME DESTINATION "$<CONFIG>/lib/${arch}"
          LIBRARY DESTINATION "$<CONFIG>/lib/${arch}")
elseif(APPLE AND NOT IOS)
  # Have to do this or else it won't be placed in the right part of the app package
  install(TARGETS toybox 
    RUNTIME DESTINATION "./../MacOS/"
    BUNDLE DESTINATION "./../MacOS/")
else()
  install(TARGETS toybox
          RUNTIME DESTINATION "."
          BUNDLE DESTINATION "."
          LIBRARY DESTINATION ".")
endif()

# Compress Textures
file(GLOB_RECURSE source_textures "${CMAKE_CURRENT_LIST_DIR}/assets/textures/*.png")

if(platform STREQUAL "android")
  set(resize "1024x1024")
else()
  set(resize "2048x2048")
endif()

set(UASTC_LEVEL $<$<CONFIG:Debug>:0>$<$<NOT:$<CONFIG:Debug>>:3>)
foreach(texture ${source_textures})
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${texture})
  get_filename_component(relpath ${relpath} DIRECTORY)
  get_filename_component(filename ${texture} NAME_WE)
  set(ktx_texture ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures/${filename}.ktx2)

  add_custom_command(
    OUTPUT ${ktx_texture}
    COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
    COMMAND ${TOKTX} --t2 --genmipmap --resize ${resize} --uastc ${UASTC_LEVEL} assets/${relpath}/${filename}.ktx2 ${texture}
    MAIN_DEPENDENCY ${texture}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  )

  list(APPEND ktx_textures ${ktx_texture})
endforeach()
add_custom_target(textures ALL DEPENDS ${ktx_textures})

# TODO: Cook scenes
file(GLOB_RECURSE source_scenes "${CMAKE_CURRENT_LIST_DIR}/assets/scenes/*.glb")
foreach(scene ${source_scenes})
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${scene})
  get_filename_component(relpath ${relpath} DIRECTORY)
  get_filename_component(filename ${scene} NAME_WE)
  set(packed_scene ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes/${filename}.glb)

  add_custom_command(
    OUTPUT ${packed_scene}
    COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
    COMMAND ${CMAKE_COMMAND} -E copy ${scene} ${packed_scene}
    MAIN_DEPENDENCY ${scene}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  )

  list(APPEND packed_scenes ${packed_scene})
endforeach()
add_custom_target(scenes ALL DEPENDS ${packed_scenes})

# Generate asset database
set(source_assets ${ktx_textures} ${packed_scenes})
list(LENGTH source_assets source_assets_num)
set(generated_manifest "assetmanifest.h")
set(manifest_contents "#pragma once\n")
string(APPEND manifest_contents "//This file is generated!\n")
string(APPEND manifest_contents "#include <stdint.h>\n")
# Add all assets to string table
# Also keep track of which assets are scenes
math(EXPR asset_idx "0")
string(APPEND manifest_contents "const uint32_t tb_asset_database_num = ${source_assets_num};\n")
string(APPEND manifest_contents "const char *tb_asset_database[${source_assets_num}] = {\n")
foreach(asset ${source_assets})
  get_filename_component(asset_ext ${asset} EXT BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
  get_filename_component(asset_path ${asset} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets ${asset_path})
  if(asset_ext MATCHES ".glb")
    list(APPEND manifest_scenes ${asset_idx})
  endif()
  string(APPEND manifest_contents "\"${relpath}\",\n")
  math(EXPR asset_idx "${asset_idx} + 1")
endforeach()
string(APPEND manifest_contents "};\n")
# Generate scene table
list(LENGTH manifest_scenes scene_database_num)
string(APPEND manifest_contents "const uint32_t tb_scene_database_num = ${scene_database_num};\n")
string(APPEND manifest_contents "const uint32_t tb_scene_database[${scene_database_num}] = {\n")
foreach(scene_idx ${manifest_scenes})
string(APPEND manifest_contents "${scene_idx},\n")
endforeach()
string(APPEND manifest_contents "};\n")

file(GENERATE OUTPUT config/${generated_manifest} CONTENT "${manifest_contents}" TARGET toybox)
add_custom_target(asset_manifest ALL DEPENDS ${generated_manifest})

# Toybox needs assets cooked
add_dependencies(toybox scenes textures asset_manifest)

# Add assets to bundle
#[[ TODO: Figure out how to get this to work with xcode
          It currently fails because ${scene} resolves to a path with a generator expression
if(APPLE)
  foreach(scene ${packed_scenes})
    set_source_files_properties(${scene} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/scenes")
    target_sources(toybox PRIVATE ${scene})
  endforeach()
  foreach(texture ${ktx_textures})
    set_source_files_properties(${texture} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/textures")
    target_sources(toybox PRIVATE ${texture})
  endforeach()
endif()
#]]

# Install cooked assets
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes
                   DESTINATION ${assets_dest})

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures
                  DESTINATION ${assets_dest}
                  FILES_MATCHING PATTERN "*.ktx2")

# Install dlls on dynamic builds
if(NOT STATIC AND WIN32)
  install(FILES $<TARGET_FILE_DIR:toybox>/SDL2$<$<CONFIG:Debug>:d>.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:toybox>/libpng16$<$<CONFIG:Debug>:d>.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:toybox>/mimalloc-redirect.dll
        DESTINATION .)
  if(MINGW)
    install(FILES $<TARGET_FILE_DIR:toybox>/libzlib$<$<CONFIG:Debug>:d>1.dll
      DESTINATION .)
    install(FILES $<TARGET_FILE_DIR:toybox>/libzstd.dll
      DESTINATION .)
  else()
    install(FILES $<TARGET_FILE_DIR:toybox>/zlib$<$<CONFIG:Debug>:d>1.dll
          DESTINATION .)
    install(FILES $<TARGET_FILE_DIR:toybox>/zstd$<$<CONFIG:Debug>:d>.dll
          DESTINATION .)
  endif()

  foreach(one_trg ${library_list})
    get_target_property(one_trg_type ${one_trg} TYPE)

    if (one_trg_type STREQUAL "SHARED_LIBRARY")
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_RELEASE)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS Release)
      endif()
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_RELWITHDEBINFO)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS RelWithDebInfo)
      endif()
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_DEBUG)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS Debug)
      endif()
    endif()
  endforeach()
endif()

if(ANDROID)
  # CPack has no support for APKs, so we do it manually
  add_subdirectory(android)
elseif(IOS)
  # TODO: custom iOS build steps
else()
  # CPack Packaging
  set(CPACK_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
  set(CPACK_PACKAGE_DESCRIPTION ${CMAKE_PROJECT_DESCRIPTION})
  set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})
  set(CPACK_PACKAGE_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH})
  set(CPACK_PACKAGE_VENDOR "Honeybunch")
  set(CPACK_PACKAGE_CONTACT "amt3824@g.rit.edu")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Honeybunch/toybox")
  set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${GIT_COMMIT_HASH}-${CMAKE_SYSTEM_NAME}")
  set(CPACK_PACKAGE_ICON ${CMAKE_CURRENT_LIST_DIR}/src/res/toybox.ico) # TODO: Move to a better folder
  set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/license.txt)
  set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_LIST_DIR}/readme.md)
  set(CPACK_PACKAGE_EXECUTABLES toybox "Toybox")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "toybox")

  # Desktop OSes
  if(WIN32)
    find_package(WIX REQUIRED)

    set(CPACK_GENERATOR WIX)

    set(CPACK_WIX_PRODUCT_GUID "044b496e-773f-447d-90c9-2a0212e1ff2c")
    set(CPACK_WIX_UPGRADE_GUID "d100fdbf-2e03-4bd0-af73-755e632fbc7a")
    set(CPACK_WIX_LICENSE_RTF ${CMAKE_CURRENT_LIST_DIR}/license.rtf)
    set(CPACK_WIX_PRODUCT_ICON ${CMAKE_CURRENT_LIST_DIR}/src/res/toybox.ico) # TODO: Move to a better folder
  elseif(APPLE)
    set(CPACK_GENERATOR Bundle)

    set(CPACK_BUNDLE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_BUNDLE_PLIST ${CMAKE_CURRENT_LIST_DIR}/macos/Info.plist)
    set(CPACK_BUNDLE_ICON ${CMAKE_CURRENT_LIST_DIR}/macos/toybox.icns)
    # TODO: Signing for the app so it can be distributed
    set(CPACK_BUNDLE_APPLE_CERT_APP "")
    set(CPACK_BUNDLE_APPLE_ENTITLEMENTS "")
  elseif(UNIX)
    # Unix systems are expected to provide the generator that suits them best
    # Don't want to try to detect which flavors of linux we're capable of creating
    # packages for.
    # TODO: Set various cpack arguments for DEB and RPM specific generation
  endif()

  include(CPack)
endif()