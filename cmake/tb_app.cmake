# Clang only for now
if(NOT ${CMAKE_C_COMPILER_ID} MATCHES "Clang" AND
   NOT ${CMAKE_C_COMPILER_ID} MATCHES "AppleClang")
  message(FATAL_ERROR "${CMAKE_C_COMPILER_ID} Unsupported - Clang only")
endif()

if(${VCPKG_TARGET_TRIPLET} MATCHES static OR ${VCPKG_TARGET_TRIPLET} MATCHES android)
  set(TB_STATIC ON)
endif()

# Set install prefix if not already overridden
set(toybox_install "${CMAKE_CURRENT_SOURCE_DIR}/install/${platform}")
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX ${toybox_install} CACHE PATH "Default Install Path" FORCE )
else()
    set(toybox_install ${CMAKE_INSTALL_PREFIX})
endif()

get_property(tb_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(tb_is_multi_config)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()

# Helper functions

function(tb_options target_name)
  if(${VCPKG_TARGET_TRIPLET} MATCHES static OR ${VCPKG_TARGET_TRIPLET} MATCHES android)
    set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  # Set known language version requirements
  target_compile_features(${target_name} PRIVATE c_std_23)
  target_compile_features(${target_name} PRIVATE cxx_std_20)

  # Turn on warnings
  target_compile_options(${target_name} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )

  if(TB_FINAL)
    target_compile_definitions(${target_name} PRIVATE "-DTB_FINAL")
  endif()
  target_compile_definitions(${target_name} PRIVATE "-DTB_CONFIG=\"$<CONFIG>\"")

  # We provide a cross platform blocks runtime so we can use this
  target_compile_options(${target_name} PRIVATE -fblocks)

  # Jolt doesn't like rtti or exceptions and neither do we
  target_compile_options(${target_name} PRIVATE -fno-rtti)
  target_compile_options(${target_name} PRIVATE -fno-exceptions)

  # Platform specifics
  if(WIN32)
    # This really only matters for clang-cl
    # ${CMAKE_C_COMPILER_ID} being MSVC would fail the above check
    # So think of this more as `if(MSVC-style-compiler)`
    if(MSVC)
      target_compile_options(${target_name} PRIVATE -Wno-reserved-id-macro)
      target_compile_options(${target_name} PRIVATE -Wno-documentation)
    endif()

    # Annoying warning in C++ when having to use flecs C macros
    target_compile_options(${target_name} PRIVATE -Wno-missing-field-initializers)
    # We use anonymous structs 
    target_compile_options(${target_name} PRIVATE -Wno-gnu-anonymous-struct)
    target_compile_options(${target_name} PRIVATE -Wno-nested-anon-types)
  elseif(ANDROID)
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "arm")
      target_link_libraries(${target_name} PRIVATE KTX::astcenc-neon-static)
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "x86")
      target_link_libraries(${target_name} PRIVATE KTX::astcenc-avx2-static)
    endif()
  endif()
endfunction()

function(tb_new_addon target_name source)
  add_library(${target_name} OBJECT ${source})

  message("Found Addon: ${target_name}")

  tb_options(${target_name})

  tb_cook_shaders(${target_name}_shader_sources ${target_name}_shader_headers)

  add_custom_target(${target_name}_shaders ALL DEPENDS "${${target_name}_shader_headers}")
  target_sources(${target_name}_shaders PRIVATE "${${target_name}_shader_sources}")
  add_dependencies(${target_name} ${target_name}_shaders)

  if(TB_COOK_ASSETS)
    tb_cook_assets(${target_name} ${target_name}_assets_path)
  endif()

  target_link_libraries(${target_name} PUBLIC ${tb_library_list})
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

  tb_options(${target_name})

  # Find config header
  set(config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
  configure_file(${CMAKE_CURRENT_LIST_DIR}/source/tb_config.h.in ${config_include_dir}/tb_config.h @ONLY)

  tb_cook_shaders(${target_name}_shader_sources ${target_name}_shader_headers)
  if(out_shader_headers)
    add_custom_target(${target_name}_shaders ALL DEPENDS ${target_name}_shader_headers)
    target_sources(${target_name}_shaders PRIVATE ${target_name}_shader_sources)
    add_dependencies(${target_name} ${target_name}_shaders)
  endif()

  if(TB_COOK_ASSETS)
    tb_cook_assets(${target_name} ${target_name}_assets_path)
  endif()

  target_link_libraries(${target_name} PRIVATE ${tb_library_list})
  target_link_libraries(${target_name} PRIVATE toybox)
  target_include_directories(${target_name} PRIVATE ${config_include_dir})
  target_include_directories(${target_name} SYSTEM PRIVATE "${CMAKE_CFG_INTDIR_ABS}/shaders")
  add_dependencies(${target_name} tb_engine_shaders)

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
  if(TB_COOK_ASSETS)
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