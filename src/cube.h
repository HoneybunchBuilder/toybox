#pragma once

#include <stddef.h>

typedef struct CPUMesh CPUMesh;

// Assumes space for entire cube has been allocated
size_t cube_alloc_size();
void create_cube(CPUMesh *cube);
