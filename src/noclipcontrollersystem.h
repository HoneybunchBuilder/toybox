#pragma once

#include "allocator.h"

#define NoClipControllerSystemId 0xFEFEFEFE

typedef struct SystemDescriptor SystemDescriptor;

// We don't actually need data for these to function so just take up a little
// space since empty structs aren't allowed
typedef uint32_t NoClipControllerSystemDescriptor;
typedef uint32_t NoClipControllerSystem;

void tb_noclip_controller_system_descriptor(
    SystemDescriptor *desc,
    const NoClipControllerSystemDescriptor *noclip_desc);
