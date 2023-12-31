#pragma once

#include "simd.h"

typedef struct TbWorld TbWorld;
typedef uint64_t ecs_entity_t;

typedef struct Projectile {
  int placeholder;
} Projectile;

typedef struct ShooterComponent {
  const char *prefab_name;
  float fire_rate;
  ecs_entity_t projectile_prefab;
  float last_fire_time;
} ShooterComponent;

#ifdef __cplusplus
extern "C" {
#endif

void tb_register_shooter_system(TbWorld *world);
void tb_unregister_shooter_system(TbWorld *world);

#ifdef __cplusplus
}
#endif
