
find_program(VCPKG vcpkg REQUIRED)

set(target ${CMAKE_ARGV3})
set(linkage ${CMAKE_ARGV4})

if(NOT target)
    message(FATAL_ERROR "Target not specified")
endif()

# Android and iOS builds are always static
if(target MATCHES "android" OR target MATCHES "ios")
    set(linkage "static")
elseif(NOT linkage)
    message(FATAL_ERROR "Linkage not specified")
endif()

set(triplet ${target}-${linkage})
if(${target} MATCHES "x64-windows" AND ${linkage} MATCHES "dynamic")
    set(triplet ${target})
elseif(${target} MATCHES "arm64-windows" AND ${linkage} MATCHES "dynamic")
    set(triplet ${target})
elseif(${target} MATCHES "x64-osx" OR ${target} MATCHES "arm64-osx")
    if(${linkage} MATCHES "static")
        set(triplet ${target})
    endif()
elseif(${target} MATCHES "x64-linux" OR ${target} MATCHES "arm64-linux")
    set(triplet ${target})
elseif(${target} MATCHES "android")
    set(triplet ${target})
endif()

cmake_host_system_information(RESULT host_plat QUERY OS_PLATFORM)
if(host_plat MATCHES "AMD64" OR host_plat MATCHES "x86_64")
    set(host_arch "x64")
elseif(host_plat MATCHES "arm")
    set(host_arch "arm64")
else()
    message(FATAL_ERROR "Unexpected host platform: ${host_plat}")
endif()

if(CMAKE_HOST_WIN32)
    set(host-triplet "${host_arch}-windows")
elseif(CMAKE_HOST_APPLE)
    set(host-triplet "${host_arch}-osx")
elseif(CMAKE_HOST_UNIX)
    set(host-triplet "${host_arch}-linux")
else()
    message(FATAL_ERROR "Unexpected host")
endif()

set(host_packages "ktx[vulkan,tools]")
set(packages "meshoptimizer;cgltf;imgui;ktx[vulkan];mimalloc;sdl2[vulkan];sdl2-image;volk;vulkan-memory-allocator;tracy")

# Determine host packages
foreach(package ${host_packages})
    list(APPEND install_list "${package}:${host-triplet}")
endforeach()

# Determine runtime packages
foreach(package ${packages})
    list(APPEND install_list "${package}:${triplet}")
endforeach()

# Install packages
execute_process(COMMAND ${VCPKG} install ${install_list})
