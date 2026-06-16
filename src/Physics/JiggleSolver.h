#pragma once

// CBPC-lite spring/damper solver
// chases the bone's REST world position with a spring+damper. 
//
// FRAMERATE NORMALIZATION: the spring is integrated in fixed-size sub-steps (kTimeTick) via a per-bone accumulator,
// so the same tuning behaves identically at 30/60/144 fps (a single variable-dt Euler step does not — its error and
// effective damping scale with dt). The output is interpolated between the two most recent ticks, which removes the
// >60 fps tick-rate judder and keeps high fps matching low fps. See bench/fps_test.cpp.

#include "RE/N/NiMatrix3.h"
#include "RE/N/NiPoint.h"

namespace OSF::Physics
{
	// Per-bone tuning (loaded from a profile JSON; see PhysicsConfig).
	struct BoneParams
	{
		RE::NiPoint3 stiffness{ 350.0f, 350.0f, 350.0f };  // spring k per local axis (higher = snappier, less lag)
		RE::NiPoint3 damping{ 12.0f, 12.0f, 12.0f };       // damper c per local axis (higher = settles faster, less wobble)
		RE::NiPoint3 gravity{ 0.0f, 0.0f, 0.0f };          // constant world-space accel (e.g. [0,0,-N] for droop)
		RE::NiPoint3 maxOffset{ 3.0f, 3.0f, 4.0f };        // |displacement| clamp per local axis (game units)
		float        mass{ 1.0f };                         // inertia: accel = springForce / mass (higher = more lag)
	};

	// Per-bone mutable state - the simulated mass in WORLD space.
	struct BoneState
	{
		RE::NiPoint3 simPos{};         // simulated world position of the jiggle mass
		RE::NiPoint3 vel{};            // world velocity of the mass
		RE::NiPoint3 prevRestWorld{};  // rest world pos last frame (teleport guard + interpolation start)
		RE::NiPoint3 prevTickTarget{}; // target rest at the previous committed tick (for tick-interval velocity)
		RE::NiPoint3 tickDisp{};       // world-space lag (simPos - target) at the latest committed tick
		RE::NiPoint3 prevTickDisp{};   // ...and at the tick before it (the two are interpolated for output)
		float        dtAccum{ 0.0f };  // unspent time carried toward the next fixed sub-step (sim lag behind render)
		bool         primed{ false };  // seeded simPos yet?

		void Reset()
		{
			vel = {};
			tickDisp = {};
			prevTickDisp = {};
			dtAccum = 0.0f;
			primed = false;  // simPos (and the tick refs) re-seed to rest on the next step
		}
	};

	// Beyond this squared world-distance jump of the rest position in one frame,
	// treat it as a teleport / cell-load and re-seat the mass instead of letting the spring chase a huge delta.
	inline constexpr float kTeleportSq = 100.0f * 100.0f;  // (100 game units)^2

	// Fixed physics sub-step. StepBone integrates the spring in whole ticks of this size and carries the remainder frame-to-frame, 
	// so the integration is the same regardless of the render frame rate 
	// (the whole point — tuning numbers settled at 60 fps then behave identically at 30/144). 1/60 s is the tuning baseline.
	inline constexpr float kTimeTick = 1.0f / 60.0f;

	// Spiral-of-death cap: never integrate more than this many sub-steps for one frame. 
	// JiggleActor::TickDt already clamps dt to <=0.1 s (=> <=6 ticks), so this only bites a pathological caller; 
	// on overflow we DROP the un-simulated backlog rather than try to catch up (which would compound the stall).
	inline constexpr int kMaxSubSteps = 8;

	// Advance one bone one frame.
	//   a_parentWorldPos/Rot = the PARENT bone's world transform this frame.
	//   a_boneLocalRest       = the bone's animated LOCAL translation this frame
	//                           (read from the rig slot before perturbing it).
	//   a_dt                  = the real (clamped) frame delta in seconds; the
	//                           solver sub-steps it internally at kTimeTick, so a
	//                           frame may integrate 0, 1, or several whole ticks.
	// Returns the local-space displacement to ADD to that local translation.
	RE::NiPoint3 StepBone(BoneState& a_state, const BoneParams& a_params,
		const RE::NiPoint3& a_parentWorldPos, const RE::NiMatrix3& a_parentWorldRot,
		const RE::NiPoint3& a_boneLocalRest, float a_dt);
}
