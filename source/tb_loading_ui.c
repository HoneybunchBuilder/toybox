#include "tb_common.h"
#include "tb_imgui.h"
#include "tb_system_priority.h"
#include "tb_world.h"

typedef struct TbLoadUICtx {
  bool visible;
} TbLoadUICtx;
ECS_COMPONENT_DECLARE(TbLoadUICtx);

void tb_load_ui_tick(ecs_iter_t *it) {
  tb_auto ecs = it->world;

  if (igBegin("Loading", NULL, 0)) {

    // For each scene root
    for (int32_t i = 0; i < it->count; ++i) {
      TbScene scene = it->entities[i];
      tb_auto scene_name = ecs_get_name(ecs, scene);
      tb_auto loaded_state =
          tb_is_scene_ready(ecs, scene) ? "Ready" : "Loading";
      igText("Scene %s - : %s", scene_name, loaded_state);
    }

    igEnd();
  }
}

void tb_register_load_ui_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbLoadUICtx);

  ECS_SYSTEM(ecs, tb_load_ui_tick, EcsOnUpdate, TbSceneRoot);

  ecs_singleton_set(ecs, TbLoadUICtx, {true});
}

void tb_unregister_load_ui_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, load_ui, TB_SYSTEM_NORMAL)
