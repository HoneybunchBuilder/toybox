cmake_minimum_required(VERSION 3.20)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# Determine version from the vcpkg json manifest
file(READ vcpkg.json VCPKG_MANIFEST)
string(JSON VCPKG_VERSION GET ${VCPKG_MANIFEST} "version")

project(toybox
        VERSION ${VCPKG_VERSION}
        DESCRIPTION "A personal game engine toybox project"
        LANGUAGES C CXX)

option(TB_BUILD_SAMPLES "Compile samples" ON)
option(FINAL "Compile with the intention to redistribute" OFF)
option(PROFILE_TRACY "Compile with support for the tracy profiler" ON)
option(COOK_ASSETS "Process assets for runtime loading" ON)

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
  if(NOT ANDROID)
    add_library(zstd::zstd ALIAS zstd::libzstd_static)
  endif()
endif()
find_package(json-c CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

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
find_program(GLTFPACK gltfpack
  PATHS ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${host_triple}/tools/meshoptimizer
  REQUIRED)

# Compile Shaders
file(GLOB shader_includes "${CMAKE_CURRENT_LIST_DIR}/include/*.hlsli")
file(GLOB shader_files "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl")

foreach(shader ${shader_files})
  get_filename_component(filename ${shader} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(vert_out_path "${shader_out_path}/${filename}_vert.h")
  set(frag_out_path "${shader_out_path}/${filename}_frag.h")
  set(out_paths "${vert_out_path};${frag_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T vs_6_5 -E vert -Vn ${filename}_vert $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${vert_out_path}
      COMMAND ${DXC} -T ps_6_5 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${frag_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

file(GLOB mesh_shader_files "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl.m")
foreach(shader ${mesh_shader_files})
  get_filename_component(filename ${shader} NAME_WLE)
  get_filename_component(filename ${filename} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(mesh_out_path "${shader_out_path}/${filename}_mesh.h")
  set(frag_out_path "${shader_out_path}/${filename}_frag.h")
  set(out_paths "${mesh_out_path};${frag_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T ms_6_5 -E mesh -Vn ${filename}_mesh $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${mesh_out_path}
      COMMAND ${DXC} -T ps_6_5 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${frag_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

file(GLOB rt_shader_files "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl.rt")
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
      COMMAND ${DXC} -T lib_6_5 -E raygen -Vn ${filename}_raygen $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${raygen_out_path}
      COMMAND ${DXC} -T lib_6_5 -E miss -Vn ${filename}_miss $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${miss_out_path}
      COMMAND ${DXC} -T lib_6_5 -E closehit -Vn ${filename}_closehit $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${closehit_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

add_custom_target(shaders ALL DEPENDS ${shader_headers})

set(library_list "SDL2::SDL2main;SDL2::SDL2_image;volk::volk;volk::volk_headers;imgui::imgui;mimalloc;mimalloc-static;KTX::ktx;json-c::json-c;meshoptimizer::meshoptimizer")

# Setup Main Executable
function(tb_add_app target_name source)
  if(WIN32)
    if(MINGW)
      add_executable(${target_name} ${source} "${CMAKE_CURRENT_LIST_DIR}/source/win32/${target_name}.rc")
      target_link_libraries(${target_name}  PRIVATE mingw32 pthread)
      target_link_options(${target_name} PRIVATE -mwindows)
    else()
      add_executable(${target_name} WIN32 ${source} "${CMAKE_CURRENT_LIST_DIR}/source/win32/${target_name}.rc")
      if(NOT MSVC)
        target_link_options(${target_name} PRIVATE -Wl,/subsystem:windows)
      endif()
    endif()
  elseif(ANDROID)
    add_library(${target_name} SHARED ${source})
  elseif(UNIX)
    if(APPLE)
      if(IOS)
        # iOS
        find_library(IMAGEIO ImageIO REQUIRED)
        find_library(CORESERVICES CoreServices REQUIRED)
        add_executable(${target_name} ${source})
        target_link_libraries(${target_name} PRIVATE ${IMAGEIO} ${CORESERVICES})
        if(CMAKE_GENERATOR MATCHES "Xcode")
          set_target_properties(${target_name} PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.honeybunch.${target_name}")
        endif()
      else()
        # macOS
        add_executable(${target_name} ${source})
      endif()
    else()
      add_executable(${target_name} ${source})
    endif()
  endif()

  target_link_libraries(${target_name} PRIVATE ${library_list})
  target_link_libraries(${target_name} PRIVATE toybox)
  # Some helpers for Visual Studio
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${target_name})
  set_property(TARGET ${target_name} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

endfunction()

file(GLOB lib_source "${CMAKE_CURRENT_LIST_DIR}/source/*.c" "${CMAKE_CURRENT_LIST_DIR}/source/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/source/*.h")
add_library(toybox STATIC ${lib_source})

if(WIN32)
  # This really only matters for clang-cl
  # ${CMAKE_C_COMPILER_ID} being MSVC would fail the above check
  # So thing of this more as if(MSVC-style-compiler)
  if(MSVC)
    target_compile_options(toybox PUBLIC -Wno-reserved-id-macro)
    target_compile_options(toybox PUBLIC -Wno-documentation)
  endif()

  # Folding VLAs to constant arrays is desired
  target_compile_options(toybox PUBLIC -Wno-gnu-folding-constant)

elseif(ANDROID)
  if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "arm")
    target_link_libraries(toybox PUBLIC KTX::astcenc-neon-static)
  elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "x86")
    target_link_libraries(toybox PUBLIC KTX::astcenc-avx2-static)
  endif()
endif()

if(NOT WIN32)
  target_sources(toybox PRIVATE "${CMAKE_CURRENT_LIST_DIR}/source/posix/entry.c")
endif()

target_sources(toybox PRIVATE ${rt_shader_files} ${shader_files} ${shader_headers})

if(isMultiConfig)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()

target_link_libraries(toybox PUBLIC ${library_list})

if(PROFILE_TRACY)
  target_link_libraries(toybox PUBLIC "Tracy::TracyClient")
endif()

# Get the latest abbreviated commit hash of the working branch
execute_process(COMMAND git log -1 --format=%h
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE)

# Generate config header
set(config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
configure_file(source/config.h.in ${config_include_dir}/config.h @ONLY)

target_include_directories(toybox PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(toybox PUBLIC "source/" "${CGLTF_INCLUDE_DIRS}" "${config_include_dir}")
# Mingw generates warnings from generated shader headers. Include as a system directory to ignore
target_include_directories(toybox SYSTEM PUBLIC "${CMAKE_CFG_INTDIR_ABS}/shaders")
target_compile_definitions(toybox PUBLIC "-DTB_CONFIG=\"$<CONFIG>\"")

if(FINAL)
  target_compile_definitions(toybox PUBLIC "-DFINAL")
endif()

if(STATIC)
  target_link_libraries(toybox PUBLIC SDL2::SDL2-static)
  set_property(TARGET toybox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_link_libraries(toybox PUBLIC SDL2::SDL2)
endif()

target_compile_features(toybox PUBLIC c_std_11)

# Turn on warnings
target_compile_options(toybox PUBLIC
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

# Cook scenes
file(GLOB_RECURSE source_scenes "${CMAKE_CURRENT_LIST_DIR}/assets/scenes/*.glb")
foreach(scene ${source_scenes})
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${scene})
  get_filename_component(relpath ${relpath} DIRECTORY)
  get_filename_component(filename ${scene} NAME_WE)
  set(packed_scene ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes/${filename}.glb)

  add_custom_command(
    OUTPUT ${packed_scene}
    COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
    COMMAND ${GLTFPACK} -cc -kn -km -ke -tc -i ${scene} -o ${packed_scene}
    MAIN_DEPENDENCY ${scene}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  )

  list(APPEND packed_scenes ${packed_scene})
endforeach()
add_custom_target(scenes DEPENDS ${packed_scenes})

# Copy textures
file(GLOB_RECURSE source_textures "${CMAKE_CURRENT_LIST_DIR}/assets/textures/*.ktx2")
foreach(texture ${source_textures})
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${texture})
  get_filename_component(relpath ${relpath} DIRECTORY)
  get_filename_component(filename ${texture} NAME_WE)
  set(packed_texture ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures/${filename}.ktx2)

  add_custom_command(
    OUTPUT ${packed_texture}
    COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
    COMMAND ${CMAKE_COMMAND} -E copy ${texture} ${packed_texture}
    MAIN_DEPENDENCY ${texture}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  )

  list(APPEND packed_textures ${packed_texture})
endforeach()
add_custom_target(textures DEPENDS ${packed_textures})

# Generate asset database
set(source_assets ${packed_scenes} ${packed_textures})
list(LENGTH source_assets ASSET_NUM)
set(generated_manifest "${config_include_dir}/assetmanifest.h")
# Add all assets to asset list
# Also keep track of which assets are scenes
math(EXPR asset_idx "0")
foreach(asset ${source_assets})
  get_filename_component(asset_ext ${asset} EXT BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
  get_filename_component(asset_path ${asset} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets ${asset_path})
  if(asset_ext MATCHES ".glb")
    list(APPEND scene_index_list ${asset_idx})
  endif()
  string(APPEND ASSET_LIST "\"${relpath}\",")
  math(EXPR asset_idx "${asset_idx} + 1")
endforeach()
# Add all scene indexes to the string that will be sent to the generated file
list(LENGTH scene_index_list SCENE_NUM)
foreach(idx ${scene_index_list})
  string(APPEND SCENE_LIST "${idx},")
endforeach()

configure_file(source/assetmanifest.h.in ${generated_manifest})
add_custom_target(asset_manifest DEPENDS ${generated_manifest})

# Toybox needs assets cooked
if(COOK_ASSETS)
  add_dependencies(toybox scenes textures asset_manifest)

  # Install cooked assets
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes
                    DESTINATION ${assets_dest})
endif()

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

if(TB_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()
