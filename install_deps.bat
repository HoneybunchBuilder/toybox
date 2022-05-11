@echo off

call vcpkg install meshoptimizer:x64-windows cgltf:x64-windows imgui:x64-windows ktx[vulkan,tools]:x64-windows mimalloc:x64-windows sdl2[vulkan]:x64-windows sdl2-image:x64-windows volk:x64-windows vulkan-memory-allocator:x64-windows tracy:x64-windows

REM Install Windows Static Deps
call vcpkg install  meshoptimizer:x64-windows-static cgltf:x64-windows-static imgui:x64-windows-static ktx[vulkan,tools]:x64-windows-static mimalloc:x64-windows-static sdl2[vulkan]:x64-windows-static sdl2-image:x64-windows-static volk:x64-windows-static vulkan-memory-allocator:x64-windows-static tracy:x64-windows-static