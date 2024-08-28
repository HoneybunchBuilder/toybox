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
option(TB_BUILD_VIEWER "Build the viewer application" ON)
option(FINAL "Compile with the intention to redistribute" OFF)
option(PROFILE_TRACY "Compile with support for the tracy profiler" ON)
option(COOK_ASSETS "Process assets for runtime loading" ON)

set(toybox_source_dir ${CMAKE_CURRENT_LIST_DIR})

set(ENGINE_NAME "toybox")
set(ENGINE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(ENGINE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(ENGINE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})

set(config $<CONFIG>)

# Get target CPU architecture
include(${CMAKE_CURRENT_LIST_DIR}/cmake/TargetArch.cmake)
target_architecture(arch)

# Platform detection
set(tb_windows 0)
set(tb_macos 0)
set(tb_linux 0)
set(tb_android 0)
set(tb_ios 0)
if(WIN32)
  set(platform "windows")
  set(tb_windows 1)
elseif(APPLE)
  if(IOS)
    set(platform "ios")
    set(tb_ios 1)
  else()
    set(platform "macos")
    set(tb_macos 1)
  endif()
elseif(ANDROID)
  set(platform "android")
  set(tb_android 1)
elseif(UNIX)
  set(platform "linux")
  set(tb_linux 1)
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

set(tb_x64 0)
set(tb_arm64 0)
if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(host_arch "x64")
  set(tb_x64 1)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(host_arch "x64")
  set(tb_x64 1)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
  set(host_arch "arm64")
  set(tb_arm64 1)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(host_arch "arm64")
  set(tb_arm64 1)
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

# Clang only for now
if(NOT ${CMAKE_C_COMPILER_ID} MATCHES "Clang" AND
   NOT ${CMAKE_C_COMPILER_ID} MATCHES "AppleClang")
  message(FATAL_ERROR "${CMAKE_C_COMPILER_ID} Unsupported - Clang only")
endif()

if(${VCPKG_TARGET_TRIPLET} MATCHES static OR ${VCPKG_TARGET_TRIPLET} MATCHES android)
  set(STATIC ON)
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(unofficial-joltphysics CONFIG REQUIRED)
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
find_package(enkiTS CONFIG REQUIRED)
find_package(mysofa CONFIG REQUIRED)
find_package(pffft CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)
find_package(steam-audio CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

find_path(CGLTF_INCLUDE_DIRS "cgltf.h")

set(VULKAN_BIN_PATH "bin")
if(platform MATCHES "windows")
  set(VULKAN_BIN_PATH "Bin")
endif()
# Prefer whatever dxc is in the vulkan SDK
# If this picks up a windows kit dxc it may not have spirv support
# but on linux a dxc on the default path will probably have spirv codegen
# so *shrug*
set(CMAKE_FIND_ROOT_PATH "$ENV{VULKAN_SDK}/${VULKAN_BIN_PATH};${CMAKE_FIND_ROOT_PATH}")
find_program(DXC dxc REQUIRED)
find_program(GLTFPACK gltfpack
  PATHS ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${host_triple}/tools/meshoptimizer
  REQUIRED)

# The list of all external targets to link
list(APPEND library_list "$<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>")
list(APPEND library_list "volk::volk")
list(APPEND library_list "volk::volk_headers")
list(APPEND library_list "imgui::imgui")
list(APPEND library_list "mimalloc")
list(APPEND library_list "mimalloc-static")
list(APPEND library_list "KTX::ktx")
list(APPEND library_list "json-c::json-c")
list(APPEND library_list "meshoptimizer::meshoptimizer")
list(APPEND library_list "unofficial::joltphysics::Jolt")
list(APPEND library_list "enkiTS::enkiTS")
list(APPEND library_list "steam-audio::phonon")
list(APPEND library_list "Vulkan::Headers")
list(APPEND library_list "GPUOpen::VulkanMemoryAllocator")

set(engine_shader_include_dir "${CMAKE_CURRENT_LIST_DIR}/include")

# Helper function to cook shaders
function(cook_shaders out_shader_sources out_shader_headers)

  # Compile Shaders
  set(shader_include_dir "${CMAKE_CURRENT_LIST_DIR}/include")
  file(GLOB shader_includes CONFIGURE_DEPENDS "${shader_include_dir}/*.hlsli" "${shader_include_dir}/*.h")

  file(GLOB shader_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl")
  foreach(shader ${shader_files})
    get_filename_component(filename ${shader} NAME_WLE)
    set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

    set(vert_out_path "${shader_out_path}/${filename}_vert.h")
    set(frag_out_path "${shader_out_path}/${filename}_frag.h")
    set(out_paths "${vert_out_path};${frag_out_path}")

    add_custom_command(
        OUTPUT ${out_paths}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
        COMMAND ${DXC} -T vs_6_5 -E vert -Vn ${filename}_vert $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-Zi> -I ${shader_include_dir} -I ${engine_shader_include_dir} $<$<NOT:$<CONFIG:Release>>:-Qembed_debug> -enable-16bit-types -fspv-target-env=vulkan1.3 -spirv ${shader} -Fh ${vert_out_path}
        COMMAND ${DXC} -T ps_6_5 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-Zi> -I ${shader_include_dir} -I ${engine_shader_include_dir} $<$<NOT:$<CONFIG:Release>>:-Qembed_debug> -enable-16bit-types -fspv-target-env=vulkan1.3 -spirv ${shader} -Fh ${frag_out_path}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )
    list(APPEND shader_headers ${out_paths})
  endforeach()
  list(APPEND shader_sources ${shader_files})

  file(GLOB mesh_shader_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl.m")
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
        COMMAND ${DXC} -T ms_6_5 -E mesh -Vn ${filename}_mesh $<$<CONFIG:Debug>:-O0> -I ${shader_include_dir} -I ${engine_shader_include_dir} $<$<NOT:$<CONFIG:Release>>:-Zi> $<$<NOT:$<CONFIG:Release>>:-Qembed_debug> -fspv-target-env=vulkan1.3 -spirv ${shader} -Fh ${mesh_out_path}
        COMMAND ${DXC} -T ps_6_5 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> -I ${shader_include_dir} -I ${engine_shader_include_dir} $<$<NOT:$<CONFIG:Release>>:-Zi> $<$<NOT:$<CONFIG:Release>>:-Qembed_debug> -fspv-target-env=vulkan1.3 -spirv ${shader} -Fh ${frag_out_path}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )
    list(APPEND shader_headers ${out_paths})
  endforeach()
  list(APPEND shader_sources ${mesh_shader_files})

  file(GLOB compute_shader_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.hlsl.cs")
  foreach(shader ${compute_shader_files})
    get_filename_component(filename ${shader} NAME_WLE)
    get_filename_component(filename ${filename} NAME_WLE)
    set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

    set(comp_out_path "${shader_out_path}/${filename}_comp.h")
    set(out_paths "${comp_out_path}")

    add_custom_command(
        OUTPUT ${out_paths}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
        COMMAND ${DXC} -T cs_6_5 -E comp -Vn ${filename}_comp $<$<CONFIG:Debug>:-O0> -I ${shader_include_dir} -I ${engine_shader_include_dir} $<$<NOT:$<CONFIG:Release>>:-Zi> $<$<NOT:$<CONFIG:Release>>:-Qembed_debug> -enable-16bit-types -fspv-target-env=vulkan1.3 -spirv ${shader} -Fh ${comp_out_path}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )
    list(APPEND shader_headers ${out_paths})
  endforeach()
  list(APPEND shader_sources ${compute_shader_files})

  set("${out_shader_sources}" "${shader_sources}" PARENT_SCOPE)
  set("${out_shader_headers}" "${shader_headers}" PARENT_SCOPE)
endfunction()

# Helper function to cook assets
function(cook_assets target_name out_assets_path)
  set(assets_dest "assets")
  if(ANDROID)
    set(assets_dest "$<CONFIG>/assets")
    install(TARGETS ${out_target_name}
            RUNTIME DESTINATION "$<CONFIG>/lib/${arch}"
            LIBRARY DESTINATION "$<CONFIG>/lib/${arch}")
  elseif(APPLE AND NOT IOS)
    # Have to do this or else it won't be placed in the right part of the app package
    install(TARGETS ${out_target_name}
      RUNTIME DESTINATION "./../MacOS/"
      BUNDLE DESTINATION "./../MacOS/")
  else()
    install(TARGETS ${out_target_name}
            RUNTIME DESTINATION "."
            BUNDLE DESTINATION "."
            LIBRARY DESTINATION ".")
  endif()

  # Cook scenes
  file(GLOB_RECURSE source_scenes CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/scenes/*.glb")
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
  list(APPEND assets ${packed_scenes})
  add_custom_target(${target_name}_scenes DEPENDS ${packed_scenes})

  # Copy textures
  file(GLOB_RECURSE source_textures CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/textures/*.ktx2")
  foreach(texture ${source_textures})
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${texture})
    get_filename_component(relpath ${relpath} DIRECTORY)
    get_filename_component(filename ${texture} NAME_WE)
    set(packed ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures/${filename}.ktx2)

    message("Copying ${texture} to ${packed}")
    add_custom_command(
      OUTPUT ${packed}
      COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
      COMMAND ${CMAKE_COMMAND} -E copy ${texture} ${packed}
      MAIN_DEPENDENCY ${texture}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND packed_textures ${packed})
  endforeach()
  list(APPEND assets ${packed_textures})
  add_custom_target(${target_name}_textures DEPENDS ${packed_textures})

  # Copy audio
  file(GLOB_RECURSE source_audio CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/audio/*.ogg" "${CMAKE_CURRENT_LIST_DIR}/assets/audio/*.wav")
  foreach(audio ${source_audio})
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${audio})
    get_filename_component(relpath ${relpath} DIRECTORY)
    get_filename_component(filename ${audio} NAME)
    set(packed ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/audio/${filename})

    message("Copying ${audio} to ${packed}")
    add_custom_command(
      OUTPUT ${packed}
      COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
      COMMAND ${CMAKE_COMMAND} -E copy ${audio} ${packed}
      MAIN_DEPENDENCY ${audio}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND packed_audio ${packed})
  endforeach()
  list(APPEND assets ${packed_audio})
  add_custom_target(${target_name}_audio DEPENDS ${packed_audio})

  # Generate asset database
  set(source_assets ${packed_scenes} ${packed_textures} ${packed_audio})
  list(LENGTH source_assets ASSET_NUM)
  set(generated_manifest "${CMAKE_CURRENT_BINARY_DIR}/config/${target_name}_assetmanifest.h")
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

  configure_file(${toybox_source_dir}/source/assetmanifest.h.in ${generated_manifest})
  add_custom_target(${target_name}_asset_manifest DEPENDS ${generated_manifest})

  add_dependencies(${target_name} ${target_name}_scenes ${target_name}_textures ${target_name}_audio ${target_name}_asset_manifest)

  # Install cooked assets
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets
                    DESTINATION ${assets_dest})

  # Add a define to the target so we know that assets were cooked
  # if they weren't we shouldn't even try to load the manifest
  target_compile_definitions(${target_name} PRIVATE -DTB_COOKED=1)

  set(${out_assets_path} ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets PARENT_SCOPE)
endfunction()

# Setup toybox library
file(GLOB_RECURSE lib_source CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.c" "${CMAKE_CURRENT_LIST_DIR}/source/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/source/*.h")
add_library(toybox OBJECT ${lib_source})

# We provide a cross platform blocks runtime so we can use this
target_compile_options(toybox PUBLIC -fblocks)

if(WIN32)
  # This really only matters for clang-cl
  # ${CMAKE_C_COMPILER_ID} being MSVC would fail the above check
  # So think of this more as `if(MSVC-style-compiler)`
  if(MSVC)
    target_compile_options(toybox PUBLIC -Wno-reserved-id-macro)
    target_compile_options(toybox PUBLIC -Wno-documentation)
  endif()

  # Annoying warning in C++ when having to use flecs C macros
  target_compile_options(toybox PUBLIC -Wno-missing-field-initializers)
  # Folding VLAs to constant arrays is desired
  target_compile_options(toybox PUBLIC -Wno-gnu-folding-constant)
  # We use anonymous structs 
  target_compile_options(toybox PUBLIC -Wno-gnu-anonymous-struct)
  target_compile_options(toybox PUBLIC -Wno-nested-anon-types)

elseif(ANDROID)
  if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "arm")
    target_link_libraries(toybox PUBLIC KTX::astcenc-neon-static)
  elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "x86")
    target_link_libraries(toybox PUBLIC KTX::astcenc-avx2-static)
  endif()
endif()

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
set(engine_config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
configure_file(source/config.h.in ${engine_config_include_dir}/tb_engine_config.h @ONLY)

target_include_directories(toybox PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(toybox PUBLIC "source/" "${CGLTF_INCLUDE_DIRS}" "${engine_config_include_dir}")
# Mingw generates warnings from generated shader headers. Include as a system directory to ignore
target_include_directories(toybox SYSTEM PUBLIC "${CMAKE_CFG_INTDIR_ABS}/shaders")
target_compile_definitions(toybox PUBLIC "-DTB_CONFIG=\"$<CONFIG>\"")
add_dependencies(toybox engine_shaders)

if(FINAL)
  target_compile_definitions(toybox PUBLIC "-DFINAL")
endif()

if(STATIC)
  target_link_libraries(toybox PUBLIC SDL3::SDL3-static)
  set_property(TARGET toybox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_link_libraries(toybox PUBLIC SDL3::SDL3)
endif()

target_link_libraries(toybox PUBLIC $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer>,SDL3_mixer::SDL3_mixer,SDL3_mixer::SDL3_mixer-static>)

# Turn on warnings
target_compile_options(toybox PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

target_compile_features(toybox PUBLIC c_std_11)
target_compile_features(toybox PUBLIC cxx_std_20)

# Cook engine shaders
cook_shaders(tb_engine_shader_sources tb_engine_shader_headers)
add_custom_target(engine_shaders ALL DEPENDS ${tb_engine_shader_headers})
target_sources(engine_shaders PRIVATE ${tb_engine_shader_sources})

# Cook engine assets
if(COOK_ASSETS)
  cook_assets(toybox engine_assets_path)
endif()

function(tb_new_addon target_name source)
  add_library(${target_name} OBJECT ${source})

  message("Found Addon: ${target_name}")

  # Set known language version requirements
  target_compile_features(${target_name} PRIVATE c_std_11)
  target_compile_features(${target_name} PRIVATE cxx_std_20)

  # Turn on warnings
  target_compile_options(${target_name} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )

  if(FINAL)
    target_compile_definitions(${target_name} PRIVATE "-DFINAL")
  endif()

  cook_shaders(${target_name}_shader_sources ${target_name}_shader_headers)

  add_custom_target(${target_name}_shaders ALL DEPENDS "${${target_name}_shader_headers}")
  target_sources(${target_name}_shaders PRIVATE "${${target_name}_shader_sources}")
  add_dependencies(${target_name} ${target_name}_shaders)

  if(COOK_ASSETS)
    cook_assets(${target_name} ${target_name}_assets_path)
  endif()

  target_link_libraries(${target_name} PUBLIC ${library_list})
  target_link_libraries(${target_name} PUBLIC toybox)
  target_include_directories(${target_name} SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/shaders")
  target_include_directories(${target_name} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
endfunction()

# Helper to describe applications that depend on toybox
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

  if(${VCPKG_TARGET_TRIPLET} MATCHES static OR ${VCPKG_TARGET_TRIPLET} MATCHES android)
    set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  # Set known language version requirements
  target_compile_features(${target_name} PRIVATE c_std_11)
  target_compile_features(${target_name} PRIVATE cxx_std_20)

  # Turn on warnings
  target_compile_options(${target_name} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )

  if(FINAL)
    target_compile_definitions(${target_name} PRIVATE "-DFINAL")
  endif()

  # Find config header
  set(config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
  configure_file(${CMAKE_CURRENT_LIST_DIR}/source/config.h.in ${config_include_dir}/config.h @ONLY)

  cook_shaders(${target_name}_shader_sources ${target_name}_shader_headers)
  if(out_shader_headers)
    add_custom_target(${target_name}_shaders ALL DEPENDS ${target_name}_shader_headers)
    target_sources(${target_name}_shaders PRIVATE ${target_name}_shader_sources)
    add_dependencies(${target_name} ${target_name}_shaders)
  endif()

  if(COOK_ASSETS)
    cook_assets(${target_name} ${target_name}_assets_path)
  endif()

  target_link_libraries(${target_name} PRIVATE ${library_list})
  target_link_libraries(${target_name} PRIVATE toybox)
  target_include_directories(${target_name} PRIVATE ${config_include_dir})
  target_include_directories(${target_name} SYSTEM PRIVATE "${CMAKE_CFG_INTDIR_ABS}/shaders")
  add_dependencies(${target_name} engine_shaders)

  if(engine_assets_path)
    # Add a target to copy engine assets
    add_custom_target(${target_name}_engine_assets 
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${engine_assets_path} ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets
    )
    add_dependencies(${target_name}_engine_assets toybox)
    add_dependencies(${target_name} ${target_name}_engine_assets)
  endif()

  # Some helpers for Visual Studio
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${target_name})
  set_property(TARGET ${target_name} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
endfunction()

function(tb_use_addon target addon)
  target_link_libraries(${target} PUBLIC ${addon})

  set(addon_asset_path "$<TARGET_PROPERTY:${addon},BINARY_DIR>/$<CONFIG>/assets")

  # Add a target to copy assets
  if(COOK_ASSETS)
    add_custom_target(${addon}_assets 
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${addon_asset_path} ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets
    )

    add_dependencies(${addon}_assets toybox)
    add_dependencies(${target}_scenes ${addon}_scenes)
    add_dependencies(${target}_textures ${addon}_textures)
    add_dependencies(${target}_audio ${addon}_audio)
    add_dependencies(${target}_asset_manifest ${addon}_asset_manifest)
    add_dependencies(${target}_engine_assets ${addon}_assets)
    add_dependencies(${target} ${addon}_assets)
  endif()
endfunction()

if(TB_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if(TB_BUILD_VIEWER)
  add_subdirectory(viewer)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/addons/CMakeLists.txt)
