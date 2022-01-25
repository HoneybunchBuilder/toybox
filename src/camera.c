#include "camera.h"

#include <SDL2/SDL_events.h>

#include "pi.h"
#include "profiling.h"

#include <stdbool.h>

void camera_projection(const Camera *c, float4x4 *p) {
  perspective(p, c->fov, c->aspect, c->near, c->far);
}

void camera_view(const Camera *c, float4x4 *v) {
  float4x4 model_matrix = {0};
  transform_to_matrix(&model_matrix, &c->transform);

  float3 forward = f4tof3(model_matrix.row2);

  look_forward(v, c->transform.position, forward, (float3){0, 1, 0});
}

void camera_sky_view(const Camera *c, float4x4 *v) {
  float4x4 model_matrix = {0};
  transform_to_matrix(&model_matrix, &c->transform);

  float3 forward = f4tof3(model_matrix.row2);

  look_forward(v, (float3){0, 0, 0}, forward, (float3){0, 1, 0});
}

void camera_view_projection(const Camera *c, float4x4 *vp) {
  float4x4 view = {0};
  camera_view(c, &view);

  float4x4 proj = {0};
  camera_projection(c, &proj);

  mulmf44(&proj, &view, vp);
}

void editor_camera_control(float delta_time_seconds, const SDL_Event *event,
                           EditorCameraController *editor, Camera *cam) {
  TracyCZoneN(ctx, "editor_camera_control", true);

  uint32_t event_type = event->type;

  EditorCameraState state = editor->state;

  // Must always clear out some state
  state &= ~EDITOR_CAMERA_LOOKING;

  // Some useful state for this function between the switch and the
  // movement calculations
  int32_t mouse_x_delta = 0;
  int32_t mouse_y_delta = 0;

  switch (event_type) {
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    EditorCameraState edit_state = 0;
    const SDL_Keysym *keysym = &event->key.keysym;
    SDL_Scancode scancode = keysym->scancode;

    if (scancode == SDL_SCANCODE_W) {
      edit_state = EDITOR_CAMERA_MOVING_FORWARD;
    } else if (scancode == SDL_SCANCODE_A) {
      edit_state = EDITOR_CAMERA_MOVING_LEFT;
    } else if (scancode == SDL_SCANCODE_S) {
      edit_state = EDITOR_CAMERA_MOVING_BACKWARD;
    } else if (scancode == SDL_SCANCODE_D) {
      edit_state = EDITOR_CAMERA_MOVING_RIGHT;
    }

    if (event_type == SDL_KEYDOWN) {
      state |= edit_state;
    } else if (event_type == SDL_KEYUP) {
      state &= ~edit_state;
    }

    break;
  }
  case SDL_MOUSEMOTION: {
    const SDL_MouseMotionEvent *mouse_motion = &event->motion;
    uint32_t button_state =
        mouse_motion->state; // What buttons are down while the mouse is moving?

    // if SOME button is pressed
    if (button_state != 0) {
      mouse_x_delta = mouse_motion->xrel;
      mouse_y_delta = mouse_motion->yrel;

      if (mouse_x_delta > 0) {
        state |= EDITOR_CAMERA_LOOKING_RIGHT;
      } else if (mouse_x_delta < 0) {
        state |= EDITOR_CAMERA_LOOKING_RIGHT;
      }
      if (mouse_y_delta > 0) {
        state |= EDITOR_CAMERA_LOOKING_DOWN;
      } else if (mouse_y_delta < 0) {
        state |= EDITOR_CAMERA_LOOKING_UP;
      }
    }

    break;
  }
  default:
    break;
  }

  if (state) {
    float4x4 mat = {0};
    transform_to_matrix(&mat, &cam->transform);

    float3 right = f4tof3(mat.row0);
    float3 forward = f4tof3(mat.row2);

    float3 velocity = {0};
    {
      float delta_move_speed = editor->move_speed * delta_time_seconds;
      if (state & EDITOR_CAMERA_MOVING_FORWARD) {
        velocity -= forward * delta_move_speed;
      }
      if (state & EDITOR_CAMERA_MOVING_LEFT) {
        velocity -= right * delta_move_speed;
      }
      if (state & EDITOR_CAMERA_MOVING_BACKWARD) {
        velocity += forward * delta_move_speed;
      }
      if (state & EDITOR_CAMERA_MOVING_RIGHT) {
        velocity += right * delta_move_speed;
      }
    }

    float3 angular_velocity = {0};
    if (state & EDITOR_CAMERA_LOOKING) {
      float delta_look_speed = editor->look_speed * delta_time_seconds;
      if (state & EDITOR_CAMERA_LOOKING_RIGHT ||
          state & EDITOR_CAMERA_LOOKING_LEFT) {
        angular_velocity[1] += mouse_x_delta * delta_look_speed;
      }
      if (state & EDITOR_CAMERA_LOOKING_DOWN ||
          state & EDITOR_CAMERA_LOOKING_UP) {
        angular_velocity[0] -= mouse_y_delta * delta_look_speed;
      }
    }

    cam->transform.position += velocity;
    cam->transform.rotation += angular_velocity;
  }

  editor->state = state;
  TracyCZoneEnd(ctx);
}
