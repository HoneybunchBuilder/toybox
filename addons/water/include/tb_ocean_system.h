#pragma once

#include "tb_allocator.h"
#include "tb_render_common.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_visual_logging_system.h"

#include <flecs.h>

#define TB_OCEAN_SYS_PRIO (TB_VLOG_SYS_PRIO + 1)

ECS_STRUCT_EXTERN(TbOceanSystem, {});
