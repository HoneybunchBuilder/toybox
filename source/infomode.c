#include "infomode.h"

#include "rigidbodycomponent.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>
#include <json.h>
#include <stdio.h>

static const char *info_mode_str = "--info";

int32_t tb_info_mode(int32_t argc, char *const *argv, TbWorld *world) {
  // Parse arguments and determine if we should write metadata to stdout
  bool info_mode = false;
  for (int32_t i = 0; i < argc; ++i) {
    const char *argument = argv[i];
    if (SDL_strncmp(argument, info_mode_str, SDL_strlen(info_mode_str)) == 0) {
      info_mode = true;
      break;
    }
  }

  if (!info_mode) {
    // Report back that the application can continue as normal
    return 0;
  }
  json_tokener *tok = json_tokener_new();

  ecs_world_t *ecs = world->ecs;

  tb_auto reflection = json_object_new_object();
  {
    char *info = ecs_type_info_to_json(ecs, ecs_id(TbRigidbodyDescriptor));
    tb_auto parsed = json_tokener_parse_ex(tok, info, SDL_strlen(info));
    json_object_object_add(reflection, "Rigidbody", parsed);
    ecs_os_free(info);
  }

  const char *refl_json = json_object_to_json_string(reflection);
  printf("%s\n", refl_json);

  json_object_put(reflection);

  json_tokener_free(tok);

  return 1;
}
