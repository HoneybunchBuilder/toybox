#!/bin/bash

# Need host tools
vcpkg install ktx[vulkan,tools]:arm64-osx

vcpkg install meshoptimizer:arm64-ios cgltf:arm64-ios imgui:arm64-ios ktx[vulkan]:arm64-ios mimalloc:arm64-ios sdl2[vulkan]:arm64-ios sdl2-image:arm64-ios volk:arm64-ios vulkan-memory-allocator:arm64-ios tracy:arm64-ios
