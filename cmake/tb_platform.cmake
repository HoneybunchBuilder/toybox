# Get target CPU architecture
include(${CMAKE_MODULE_PATH}/TargetArch.cmake)
target_architecture(arch)

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

set(tb_host_triplet "${host_arch}-${host}")