#!/bin/bash

cd ./vcpkg

./vcpkg install meshoptimizer:x64-linux cgltf:x64-linux imgui:x64-linux ktx[vulkan,tools]:x64-linux mimalloc:x64-linux sdl2[vulkan]:x64-linux sdl2-image:x64-linux volk:x64-linux vulkan-memory-allocator:x64-linux tracy:x64-linux

cd ../