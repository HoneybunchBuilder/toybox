
find_program(VCPKG vcpkg REQUIRED)

set(triplet ${CMAKE_ARGV3})

if(NOT triplet)
    message(FATAL_ERROR "Triplet not specified")
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
