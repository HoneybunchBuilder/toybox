#pragma once

#include "simd.h"

#define WindComponentId 0xDEAD0000
#define WindComponentIdStr "0xDEAD0000"
#define BoatMovementComponentId 0xDEAD0001
#define BoatMovementComponentIdStr "0xDEAD0001"
#define MastComponentId 0xDEAD0002
#define MastComponentIdStr "0xDEAD0002"

// Tracks the state of the wind
// Currently this is intended to mostly be a single use global component
// but in the future the idea is to look up this component based on
// some sort of weather pattern lookup
typedef struct WindComponent {
  float3 direction;
  float strength;
} WindComponent;

// State for managing movement of the boat
// Both the speed and heading of the ship as well as how it rotates and bobs on
// the waves
typedef struct BoatMovementComponent {
  float bouyancy; // How fast the boat will lerp to the target height
  float target_height_offset; // Target height offset to move to

  float heading_change_speed; // How fast the boat will face the target heading
  float3 target_heading;      // Direction we want the boat to face
  // Current heading is the attached transform component's forward

  float max_acceleration;
  float acceleration;
  float max_speed;
  float speed;

  float inertia;  // The magnitude of velocity required to start moving
  float friction; // How fast the boat will come to a stop
} BoatMovementComponent;

typedef struct BoatMovementComponentDesc {
  float bouyancy;
  float heading_change_speed;
  float max_acceleration;
  float max_speed;
  float inertia;
  float friction;
} BoatMovementComponentDesc;

typedef struct MastComponent {
  float heading_change_speed; // How fast the mast will face the target heading
  float3 target_heading;      // Direction we want the mast to face
} MastComponent;

typedef struct ComponentDescriptor ComponentDescriptor;

void tb_wind_component_descriptor(ComponentDescriptor *desc);
void tb_boat_movement_component_descriptor(ComponentDescriptor *desc);
void tb_mast_component_descriptor(ComponentDescriptor *desc);
