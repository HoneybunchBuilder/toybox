@echo off

cd ./vcpkg

REM Install Switch Deps
call vcpkg install meshoptimizer:arm64-switch cgltf:arm64-switch imgui:arm64-switch ktx[vulkan]:arm64-switch mimalloc:arm64-switch sdl2[vulkan]:arm64-switch sdl2-image:arm64-switch volk:arm64-switch vulkan-memory-allocator:arm64-switch tracy:arm64-switch

REM Return to starting directory
cd %~dp0