if(NOT _VCPKG_SWITCH_TOOLCHAIN)
set(_VCPKG_SWITCH_TOOLCHAIN 1)

# dkp-initialize-path.cmake
if(NOT DEFINED ENV{DEVKITPRO})
  set(DEVKITPRO /opt/devkitpro)
else()
  set(DEVKITPRO $ENV{DEVKITPRO})
endif()
file(TO_CMAKE_PATH ${DEVKITPRO} DEVKITPRO)

list(APPEND CMAKE_MODULE_PATH "${DEVKITPRO}/cmake")

# devkitA64.cmake
set(devkitA64 TRUE)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR "aarch64")
set(CMAKE_CROSSCOMPILING 1)

set(TOOL_PREFIX ${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-)

# Small modification for windows
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_ASM_COMPILER ${TOOL_PREFIX}gcc.exe    CACHE PATH "")
    set(CMAKE_C_COMPILER   ${TOOL_PREFIX}gcc.exe    CACHE PATH "")
    set(CMAKE_CXX_COMPILER ${TOOL_PREFIX}g++.exe    CACHE PATH "")
    set(CMAKE_LINKER       ${TOOL_PREFIX}g++.exe    CACHE PATH "")
    set(CMAKE_AR           ${TOOL_PREFIX}ar.exe     CACHE PATH "")
    set(CMAKE_RANLIB       ${TOOL_PREFIX}ranlib.exe CACHE PATH "")
    set(CMAKE_STRIP        ${TOOL_PREFIX}strip.exe  CACHE PATH "")
else()
    set(CMAKE_ASM_COMPILER ${TOOL_PREFIX}gcc    CACHE PATH "")
    set(CMAKE_C_COMPILER   ${TOOL_PREFIX}gcc    CACHE PATH "")
    set(CMAKE_CXX_COMPILER ${TOOL_PREFIX}g++    CACHE PATH "")
    set(CMAKE_LINKER       ${TOOL_PREFIX}g++    CACHE PATH "")
    set(CMAKE_AR           ${TOOL_PREFIX}ar     CACHE PATH "")
    set(CMAKE_RANLIB       ${TOOL_PREFIX}ranlib CACHE PATH "")
    set(CMAKE_STRIP        ${TOOL_PREFIX}strip  CACHE PATH "")
endif()

# small modification to let cmake test for switch
set(SWITCH ON CACHE BOOL "")

set(CMAKE_LIBRARY_ARCHITECTURE aarch64-none-elf CACHE INTERNAL "abi")

set(CMAKE_FIND_ROOT_PATH
  ${DEVKITPRO}/devkitA64
  ${DEVKITPRO}/devkitA64/aarch64-none-elf
  ${DEVKITPRO}/tools
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Shared libs not available")

# dkp-custom-target.cmake
function(dkp_set_target_file target file)
  set_target_properties(${target} PROPERTIES DKP_FILE "${file}")
endfunction()

function(dkp_resolve_file outvar inname)
  if (TARGET "${inname}")
    get_target_property(_filename "${inname}" DKP_FILE)
    if (NOT _filename)
      message(FATAL_ERROR "dkp_resolve_file: target ${inname} does not have a registered file")
    endif()
    set(${outvar} "${_filename}" PARENT_SCOPE)
  elseif(EXISTS "${inname}")
    set(${outvar} "${inname}" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "dkp_resolve_file: unable to resolve file: ${inname}")
  endif()
endfunction()

#dkp-embedded-binary.cmake

find_program(BIN2S_EXE NAMES bin2s HINTS "${DEVKITPRO}/tools/bin")
if (NOT BIN2S_EXE)
  message(WARNING "Could not find bin2s: try installing general-tools")
endif()

function(dkp_generate_binary_embed_sources outvar)
  if (NOT ${ARGC} GREATER 1)
    message(FATAL_ERROR "dkp_generate_binary_embed_sources: must provide at least one input file")
  endif()

  set(outlist "")
  foreach (inname ${ARGN})
    dkp_resolve_file(infile "${inname}")
    get_filename_component(basename "${infile}" NAME)
    string(REPLACE "." "_" basename "${basename}")

    if (TARGET "${inname}")
      set(indeps ${inname} ${infile})
    else()
      set(indeps ${infile})
    endif()

    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${basename}.s" "${CMAKE_CURRENT_BINARY_DIR}/${basename}.h"
      COMMAND ${BIN2S_EXE} -H "${CMAKE_CURRENT_BINARY_DIR}/${basename}.h" "${infile}" > "${CMAKE_CURRENT_BINARY_DIR}/${basename}.s"
      DEPENDS ${indeps}
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      COMMENT "Generating binary embedding source for ${inname}"
    )

    list(APPEND outlist "${CMAKE_CURRENT_BINARY_DIR}/${basename}.s" "${CMAKE_CURRENT_BINARY_DIR}/${basename}.h")
  endforeach()

  set(${outvar} "${outlist}" PARENT_SCOPE)
endfunction()

function(dkp_add_embedded_binary_library target)
  dkp_generate_binary_embed_sources(intermediates ${ARGN})
  add_library(${target} OBJECT ${intermediates})
  target_include_directories(${target} INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

function(dkp_target_use_embedded_binary_libraries target)
  if (NOT ${ARGC} GREATER 1)
    message(FATAL_ERROR "dkp_target_use_embedded_binary_libraries: must provide at least one input library")
  endif()

  foreach (libname ${ARGN})
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${libname}>)
    target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:${libname},INTERFACE_INCLUDE_DIRECTORIES>)
  endforeach()
endfunction()

#dkp-asset-folder.cmake

function(dkp_add_asset_target target folder)
  if (NOT IS_ABSOLUTE "${folder}")
    set(folder "${CMAKE_CURRENT_LIST_DIR}/${folder}")
  endif()

  add_custom_target(${target})
  set_target_properties(${target} PROPERTIES
    DKP_ASSET_FOLDER "${folder}"
    DKP_ASSET_FILES ""
  )
endfunction()

function(dkp_install_assets target)
  get_target_property(_dest ${target} DKP_ASSET_FOLDER)
  if (NOT _dest)
    message(FATAL_ERROR "dkp_install_assets: ${target} is not a valid asset target")
  endif()

  cmake_parse_arguments(ASSET "" "DESTINATION" "TARGETS" ${ARGN})

  if (DEFINED ASSET_DESTINATION)
    set(_dest "${_dest}/${ASSET_DESTINATION}")
  endif()

  if (NOT DEFINED ASSET_TARGETS)
    message(FATAL_ERROR "dkp_install_assets: must specify at least one target to install")
  endif()

  foreach (srctarget ${ASSET_TARGETS})
    if (NOT TARGET ${srctarget})
      message(FATAL_ERROR "dkp_install_assets: target ${srctarget} not found")
    endif()

    if (TARGET ${srctarget}_install_to_${target})
      message(WARNING "dkp_install_assets: target ${srctarget} already installed to ${target}, skipping")
      continue()
    endif()

    dkp_resolve_file(_srcfile ${srctarget})
    get_filename_component(_name "${_srcfile}" NAME)
    set(_destfile "${_dest}/${_name}")

    add_custom_command(
      OUTPUT "${_destfile}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
      COMMAND ${CMAKE_COMMAND} -E copy "${_srcfile}" "${_destfile}"
      COMMENT "Installing ${srctarget} to ${target}"
      DEPENDS ${srctarget} "${_srcfile}"
    )

    add_custom_target(${srctarget}_install_to_${target}
      DEPENDS "${_destfile}"
    )

    add_dependencies(${target} ${srctarget}_install_to_${target})
    set_property(TARGET ${target} APPEND PROPERTY DKP_ASSET_FILES "${_destfile}")
  endforeach()
endfunction()

function(dkp_track_assets target)
  get_target_property(_dest ${target} DKP_ASSET_FOLDER)
  if (NOT _dest)
    message(FATAL_ERROR "dkp_track_assets: ${target} is not a valid asset target")
  endif()

  cmake_parse_arguments(ASSET "" "FOLDER" "FILES" ${ARGN})

  if (DEFINED ASSET_FOLDER)
    set(_dest "${_dest}/${ASSET_FOLDER}")
  endif()

  if (NOT DEFINED ASSET_FILES)
    message(FATAL_ERROR "dkp_track_assets: must provide at least one input file")
  endif()

  foreach (file ${ASSET_FILES})
    set(file "${_dest}/${file}")
    if (NOT EXISTS "${file}")
      message(FATAL_ERROR "dkp_track_assets: file ${file} does not exist")
    endif()

    set_property(TARGET ${target} APPEND PROPERTY DKP_ASSET_FILES "${file}")
  endforeach()
endfunction()

# switch.cmake

set(NX_ARCH_SETTINGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec")
set(NX_COMMON_FLAGS  "${NX_ARCH_SETTINGS} -ffunction-sections -fdata-sections -D__SWITCH__")
set(NX_LIB_DIRS      "-L${DEVKITPRO}/libnx/lib -L${DEVKITPRO}/portlibs/switch/lib")

set(CMAKE_C_FLAGS_INIT   "${NX_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${NX_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${NX_COMMON_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${NX_ARCH_SETTINGS} ${NX_LIB_DIRS} -fPIE -specs=${DEVKITPRO}/libnx/switch.specs")

list(APPEND CMAKE_FIND_ROOT_PATH
  ${DEVKITPRO}/portlibs/switch
  ${DEVKITPRO}/libnx
)

# Start find_package in config mode
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

# Set pkg-config for the same
find_program(PKG_CONFIG_EXECUTABLE NAMES aarch64-none-elf-pkg-config HINTS "${DEVKITPRO}/portlibs/switch/bin")
if (NOT PKG_CONFIG_EXECUTABLE)
  message(WARNING "Could not find aarch64-none-elf-pkg-config: try installing switch-pkg-config")
endif()

find_program(NX_ELF2NRO_EXE NAMES elf2nro HINTS "${DEVKITPRO}/tools/bin")
if (NOT NX_ELF2NRO_EXE)
  message(WARNING "Could not find elf2nro: try installing switch-tools")
endif()

find_program(NX_NACPTOOL_EXE NAMES nacptool HINTS "${DEVKITPRO}/tools/bin")
if (NOT NX_NACPTOOL_EXE)
  message(WARNING "Could not find nacptool: try installing switch-tools")
endif()

find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
if (NOT NX_UAM_EXE)
  message(WARNING "Could not find uam: try installing uam")
endif()

find_file(NX_DEFAULT_ICON NAMES default_icon.jpg HINTS "${DEVKITPRO}/libnx" NO_CMAKE_FIND_ROOT_PATH)
if (NOT NX_DEFAULT_ICON)
  message(WARNING "Could not find default icon: try installing libnx")
endif()

set(NINTENDO_SWITCH TRUE)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(NX_ROOT ${DEVKITPRO}/libnx)

set(NX_STANDARD_LIBRARIES "-lnx")
set(CMAKE_C_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "" FORCE)
set(CMAKE_ASM_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "" FORCE)

#for some reason cmake (3.14.3) doesn't appreciate having \" here
set(NX_STANDARD_INCLUDE_DIRECTORIES "${NX_ROOT}/include")
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")
set(CMAKE_ASM_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")

function(nx_generate_nacp outfile)
  cmake_parse_arguments(NACP "" "NAME;AUTHOR;VERSION" "" ${ARGN})
  if (NOT DEFINED NACP_NAME)
    set(NACP_NAME "${CMAKE_PROJECT_NAME}")
  endif()
  if (NOT DEFINED NACP_AUTHOR)
    set(NACP_AUTHOR "Unspecified Author")
  endif()
  if (NOT DEFINED NACP_VERSION)
    if (PROJECT_VERSION)
      set(NACP_VERSION "${PROJECT_VERSION}")
    else()
      set(NACP_VERSION "1.0.0")
    endif()
  endif()

  add_custom_command(
    OUTPUT "${outfile}"
    COMMAND "${NX_NACPTOOL_EXE}" --create "${NACP_NAME}" "${NACP_AUTHOR}" "${NACP_VERSION}" "${outfile}"
    VERBATIM
  )
endfunction()

function(nx_create_nro target)
  cmake_parse_arguments(ELF2NRO "NOICON;NONACP" "ICON;NACP;ROMFS" "" ${ARGN})

  get_target_property(TARGET_OUTPUT_NAME ${target} OUTPUT_NAME)
  if(NOT TARGET_OUTPUT_NAME)
    set(TARGET_OUTPUT_NAME "${target}")
  endif()

  set(ELF2NRO_ARGS "$<TARGET_FILE:${target}>" "${TARGET_OUTPUT_NAME}.nro")
  set(ELF2NRO_DEPS ${target})

  if (DEFINED ELF2NRO_ICON AND ELF2NRO_NOICON)
    message(FATAL_ERROR "nx_create_nro: cannot specify ICON and NOICON at the same time")
  endif()

  if (DEFINED ELF2NRO_NACP AND ELF2NRO_NONACP)
    message(FATAL_ERROR "nx_create_nro: cannot specify NACP and NONACP at the same time")
  endif()

  if (NOT DEFINED ELF2NRO_ICON AND NOT ELF2NRO_NOICON)
    set(ELF2NRO_ICON "${NX_DEFAULT_ICON}")
  endif()

  if (NOT DEFINED ELF2NRO_NACP AND NOT ELF2NRO_NONACP)
    set(ELF2NRO_NACP "${target}.default.nacp")
    nx_generate_nacp(${ELF2NRO_NACP})
  endif()

  if (DEFINED ELF2NRO_ICON)
    list(APPEND ELF2NRO_ARGS "--icon=${ELF2NRO_ICON}")
    list(APPEND ELF2NRO_DEPS "${ELF2NRO_ICON}")
  endif()

  if (DEFINED ELF2NRO_NACP)
    list(APPEND ELF2NRO_ARGS "--nacp=${ELF2NRO_NACP}")
    list(APPEND ELF2NRO_DEPS "${ELF2NRO_NACP}")
  endif()

  if (DEFINED ELF2NRO_ROMFS)
    if (TARGET "${ELF2NRO_ROMFS}")
      get_target_property(_folder "${ELF2NRO_ROMFS}" DKP_ASSET_FOLDER)
      if (NOT _folder)
        message(FATAL_ERROR "nx_create_nro: not a valid asset target")
      endif()
      list(APPEND ELF2NRO_ARGS "--romfsdir=${_folder}")
      list(APPEND ELF2NRO_DEPS ${ELF2NRO_ROMFS} $<TARGET_PROPERTY:${ELF2NRO_ROMFS},DKP_ASSET_FILES>)
    else()
      if (NOT IS_ABSOLUTE "${ELF2NRO_ROMFS}")
        set(ELF2NRO_ROMFS "${CMAKE_CURRENT_LIST_DIR}/${ELF2NRO_ROMFS}")
      endif()
      if (NOT IS_DIRECTORY "${ELF2NRO_ROMFS}")
        message(FATAL_ERROR "nx_create_nro: cannot find romfs dir: ${ELF2NRO_ROMFS}")
      endif()
      list(APPEND ELF2NRO_ARGS "--romfsdir=${ELF2NRO_ROMFS}")
    endif()
  endif()

  add_custom_command(
    OUTPUT "${TARGET_OUTPUT_NAME}.nro"
    COMMAND "${NX_ELF2NRO_EXE}" ${ELF2NRO_ARGS}
    DEPENDS ${ELF2NRO_DEPS}
    VERBATIM
  )

  add_custom_target(
    "${target}_nro" ALL
    DEPENDS "${TARGET_OUTPUT_NAME}.nro"
  )
endfunction()

function(nx_add_shader_program target source type)
  if(NOT IS_ABSOLUTE ${source})
    set(source "${CMAKE_CURRENT_LIST_DIR}/${source}")
  endif()

  set(outfile "${CMAKE_CURRENT_BINARY_DIR}/${target}.dksh")
  add_custom_command(
    OUTPUT "${outfile}"
    COMMAND "${NX_UAM_EXE}" -o "${outfile}" -s ${type} "${source}"
    DEPENDS "${source}"
    COMMENT "Building shader program ${target}"
  )
  add_custom_target(${target} DEPENDS "${outfile}")
  dkp_set_target_file(${target} "${outfile}")
endfunction()

endif()