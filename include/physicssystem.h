#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbWorld TbWorld;

void tb_register_physics_sys(TbWorld *world);
void tb_unregister_physics_sys(TbWorld *world);

#ifdef __cplusplus
}
#endif
