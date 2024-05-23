#pragma once

#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tb_allocator.h"
#include "tbrendercommon.h"
#include "visualloggingsystem.h"

#include <flecs.h>

#define TB_OCEAN_SYS_PRIO (TB_VLOG_SYS_PRIO + 1)

ECS_STRUCT_EXTERN(TbOceanSystem, {});
