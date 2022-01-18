#pragma once

#include "simd.h"

typedef struct CPUMesh CPUMesh;
typedef struct Allocator Allocator;

CPUMesh *create_skydome(Allocator *a);
