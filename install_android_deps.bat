@echo off

cd ./vcpkg

REM Install Windows Host Tools
call vcpkg install tool-dxc:x64-windows tool-gltfpack:x64-windows ktx[vulkan,tools]:x64-windows

REM Install arm64 Android Deps
call vcpkg install meshoptimizer:arm64-android cgltf:arm64-android imgui:arm64-android vulkan:arm64-android vulkan-headers:arm64-android ktx[vulkan]:arm64-android mimalloc:arm64-android sdl2[vulkan]:arm64-android sdl2-image:arm64-android volk:arm64-android vulkan-memory-allocator:arm64-android tracy:arm64-android

REM Install x64 Android Deps
call vcpkg install meshoptimizer:x64-android cgltf:x64-android imgui:x64-android vulkan:x64-android vulkan-headers:x64-android ktx[vulkan]:x64-android mimalloc:x64-android sdl2[vulkan]:x64-android sdl2-image:x64-android volk:x64-android vulkan-memory-allocator:x64-android tracy:x64-android

REM Return to starting directory
cd %~dp0