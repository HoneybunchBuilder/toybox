#pragma once

#include <stdint.h>

#include "allocator.h"
#include "simd.h"

typedef struct VkDevice_T *VkDevice;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaPool_T *VmaPool;
typedef struct GPUMesh GPUMesh;
typedef struct GPUTexture GPUTexture;
typedef struct GPUMaterial GPUMaterial;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

enum ComponentType {
  COMPONENT_TYPE_NONE = 0x00000000,
  COMPONENT_TYPE_TRANSFORM = 0x00000001,
  COMPONENT_TYPE_STATIC_MESH = 0x00000002,
  COMPONENT_TYPE_MATERIAL = 0x00000004,
};

#define MAX_CHILD_COUNT 2048
typedef struct SceneTransform {
  Transform t;
  uint32_t child_count;
  uint32_t children[MAX_CHILD_COUNT];
} SceneTransform;

typedef struct DemoAllocContext {
  VkDevice device;
  Allocator tmp_alloc;
  Allocator std_alloc;
  const VkAllocationCallbacks *vk_alloc;
  VmaAllocator vma_alloc;
  VmaPool up_pool;
  VmaPool tex_pool;
} DemoAllocContext;

typedef struct Scene {
  bool loaded;
  DemoAllocContext alloc_ctx;

  uint32_t entity_count;
  uint64_t *components;

  SceneTransform *transforms;
  uint32_t *static_mesh_refs;
  uint32_t *material_refs;

  uint32_t mesh_count;
  GPUMesh *meshes;

  uint32_t texture_count;
  GPUTexture *textures;

  uint32_t material_count;
  GPUMaterial *materials;
} Scene;

int32_t create_scene(DemoAllocContext alloc_ctx, Scene *out_scene);
int32_t scene_append_gltf(Scene *s, const char *filename);
void destroy_scene(Scene *s);
