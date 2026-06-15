#pragma once

// CBPC-lite spring/damper solver — pure math over CLSF NiPoint3/NiMatrix3, no
// engine calls. Position-based model (the one CBP/CBPC use): a simulated mass
// chases the bone's REST world position with a spring+damper. Smooth by
// construction — there is no noisy 2nd-derivative; jiggle emerges when the rest
// position accelerates (footsteps, start/stop, turns). One BoneState per bone;
// StepBone advances it one frame and returns the LOCAL-space displacement to ADD
// to the bone's animated local translation.

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

	// Per-bone mutable state — the simulated mass in WORLD space.
	struct BoneState
	{
		RE::NiPoint3 simPos{};         // simulated world position of the jiggle mass
		RE::NiPoint3 vel{};            // world velocity of the mass
		RE::NiPoint3 prevRestWorld{};  // rest world pos last frame (teleport guard)
		bool         primed{ false };  // seeded simPos yet?

		void Reset()
		{
			vel = {};
			primed = false;  // simPos re-seeds to rest on the next step
		}
	};

	// Beyond this squared world-distance jump of the rest position in one frame,
	// treat it as a teleport / cell-load and re-seat the mass instead of letting
	// the spring chase a huge delta.
	inline constexpr float kTeleportSq = 100.0f * 100.0f;  // (100 game units)^2

	// Advance one bone one frame.
	//   a_parentWorldPos/Rot = the PARENT bone's world transform this frame.
	//   a_boneLocalRest       = the bone's animated LOCAL translation this frame
	//                           (read from the rig slot before perturbing it).
	// Returns the local-space displacement to ADD to that local translation.
	RE::NiPoint3 StepBone(BoneState& a_state, const BoneParams& a_params,
		const RE::NiPoint3& a_parentWorldPos, const RE::NiMatrix3& a_parentWorldRot,
		const RE::NiPoint3& a_boneLocalRest, float a_dt);
}
