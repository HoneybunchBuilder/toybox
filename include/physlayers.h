#pragma once

#ifdef __cplusplus

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

// Layer that objects can be in, determines which other objects it can collide
// with Typically you at least want to have 1 layer for moving bodies and 1
// layer for static bodies, but you can have more layers if you want. E.g. you
// could have a layer for high detail collision (which is not used by the
// physics simulation but only if you do collision testing).
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}; // namespace Layers

// Each broadphase layer results in a separate bounding volume tree in the
// broad phase. You at least want to have a layer for non-moving and moving
// objects to avoid having to update a tree full of static objects every
// frame. You can have a 1-on-1 mapping between object layers and broadphase
// layers (like in this case) but if you have many object layers you'll be
// creating many broad phase trees, which is not efficient. If you want to
// fine tune your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look
// at the stats reported on the TTY.
namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr uint32_t NUM_LAYERS(2);
}; // namespace BroadPhaseLayers

#endif
