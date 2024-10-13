include(${CMAKE_MODULE_PATH}/tb_platform.cmake)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
if(TB_PROFILE_TRACY)
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

find_program(GLTFPACK gltfpack
  PATHS ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${tb_host_triplet}/tools/meshoptimizer
  REQUIRED)
find_program(SLANG slangc 
  PATHS ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${tb_host_triplet}/tools/shader-slang
  REQUIRED)

# The list of all external targets to link
list(APPEND tb_library_list "$<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>")
list(APPEND tb_library_list "volk::volk")
list(APPEND tb_library_list "volk::volk_headers")
list(APPEND tb_library_list "imgui::imgui")
list(APPEND tb_library_list "mimalloc")
list(APPEND tb_library_list "mimalloc-static")
list(APPEND tb_library_list "KTX::ktx")
list(APPEND tb_library_list "json-c::json-c")
list(APPEND tb_library_list "meshoptimizer::meshoptimizer")
list(APPEND tb_library_list "Jolt::Jolt")
list(APPEND tb_library_list "enkiTS::enkiTS")
list(APPEND tb_library_list "steam-audio::phonon")
list(APPEND tb_library_list "GPUOpen::VulkanMemoryAllocator")