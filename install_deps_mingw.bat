@echo off

call vcpkg install meshoptimizer:x64-mingw-dynamic cgltf:x64-mingw-dynamic imgui:x64-mingw-dynamic ktx[vulkan,tools]:x64-mingw-dynamic mimalloc:x64-mingw-dynamic sdl2[vulkan]:x64-mingw-dynamic sdl2-image:x64-mingw-dynamic volk:x64-mingw-dynamic vulkan-memory-allocator:x64-mingw-dynamic tracy:x64-mingw-dynamic --recurse

REM Install Mingw Static Deps
call vcpkg install meshoptimizer:x64-mingw-static cgltf:x64-mingw-static imgui:x64-mingw-static ktx[vulkan,tools]:x64-mingw-static mimalloc:x64-mingw-static sdl2[vulkan]:x64-mingw-static sdl2-image:x64-mingw-static volk:x64-mingw-static vulkan-memory-allocator:x64-mingw-static tracy:x64-mingw-static --recurse
