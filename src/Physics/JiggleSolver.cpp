#include "Physics/JiggleSolver.h"

#include "Util/Math.h"

namespace OSF::Physics
{
	RE::NiPoint3 StepBone(BoneState& a_state, const BoneParams& a_params,
		const RE::NiPoint3& a_parentWorldPos, const RE::NiMatrix3& a_parentWorldRot,
		const RE::NiPoint3& a_boneLocalRest, float a_dt)
	{
		// The bone's REST world position = parentWorld * boneLocalRest.
		// (rotate * localTranslate + parentTranslate; scale assumed 1.)
		const RE::NiPoint3 restWorld = a_parentWorldPos + a_parentWorldRot * a_boneLocalRest;

		// First valid frame: seat the mass at rest, no displacement.
		if (!a_state.primed) {
			a_state.simPos = restWorld;
			a_state.vel = {};
			a_state.prevRestWorld = restWorld;
			a_state.dtAccum = 0.0f;
			a_state.primed = true;
			return {};
		}

		// Teleport / cell-load guard: a huge one-frame jump of the rest position
		// is not motion — re-seat the mass so the spring doesn't fling it.
		if (restWorld.GetSquaredDistance(a_state.prevRestWorld) > kTeleportSq) {
			a_state.simPos = restWorld;
			a_state.vel = {};
			a_state.prevRestWorld = restWorld;
			a_state.dtAccum = 0.0f;
			return {};
		}
		// Velocity of the rest target this frame (for relative damping below). This
		// is a real-world units/sec rate measured over the frame, so it stays valid
		// inside the fixed-size sub-steps regardless of how many we take.
		const RE::NiPoint3 prevRest = a_state.prevRestWorld;
		const float invDt = 1.0f / a_dt;
		const RE::NiPoint3 restVel = (restWorld - prevRest) * invDt;
		a_state.prevRestWorld = restWorld;
		const float invMass = (a_params.mass > 1e-4f) ? (1.0f / a_params.mass) : 0.0f;

		// Fixed-timestep accumulator: bank this frame's dt and integrate as many
		// whole kTimeTick sub-steps as it now affords, carrying the remainder. The
		// integration dt is therefore constant (kTimeTick) no matter the frame rate,
		// which is what makes the tuning framerate-independent. Cap the per-frame
		// step count and drop the backlog on overflow so a low-fps spike can't spiral.
		a_state.dtAccum += a_dt;
		int steps = static_cast<int>(a_state.dtAccum / kTimeTick);  // floor; dtAccum >= 0
		if (steps > kMaxSubSteps) {
			steps = kMaxSubSteps;
			a_state.dtAccum = 0.0f;  // discard time we will never catch up on
		} else {
			a_state.dtAccum -= static_cast<float>(steps) * kTimeTick;
		}

		// Sweep the rest target across the sub-steps from last frame's value to this
		// frame's, one increment per tick, so each fixed step is forced by the target
		// at ~its own point in time. Holding the end-of-frame target for every
		// sub-step would make the forcing lead the sim by up to half a frame at low
		// fps but not at high fps — a framerate-dependent error that defeats the whole
		// fixed-timestep exercise. Linear is exact for constant-velocity locomotion;
		// for curved motion it's a bounded approximation (we only get one parent
		// sample per frame anyway).
		const RE::NiPoint3 restStep = (steps > 0)
			? (restWorld - prevRest) * (1.0f / static_cast<float>(steps))
			: RE::NiPoint3{};

		// Spring toward the (swept) rest target; damp the mass's velocity RELATIVE to
		// the rest target's velocity (vel - restVel). This velocity feed-forward makes
		// the mass track steady locomotion with NO sustained lag, so jiggle is purely
		// transient — it reacts to acceleration (footsteps, start/stop) and returns to
		// rest, like the native ragdoll constraint. The maxOffset clamp plays the role
		// of the cone/twist limit. accel = (k*(rest-sim) - c*(vel-restVel))/mass + g,
		// and the relative damping stays INSIDE the loop (NOT CBPC's absolute damping).
		RE::NiPoint3 target = prevRest;
		for (int i = 0; i < steps; ++i) {
			target = target + restStep;
			const RE::NiPoint3 toRest = target - a_state.simPos;
			const RE::NiPoint3 accel{
				(a_params.stiffness.x * toRest.x - a_params.damping.x * (a_state.vel.x - restVel.x)) * invMass + a_params.gravity.x,
				(a_params.stiffness.y * toRest.y - a_params.damping.y * (a_state.vel.y - restVel.y)) * invMass + a_params.gravity.y,
				(a_params.stiffness.z * toRest.z - a_params.damping.z * (a_state.vel.z - restVel.z)) * invMass + a_params.gravity.z
			};

			// Semi-implicit Euler at the fixed sub-step dt.
			a_state.vel = a_state.vel + accel * kTimeTick;
			a_state.simPos = a_state.simPos + a_state.vel * kTimeTick;
		}

		// Render extrapolation: advance the mass past the last whole tick by the
		// unspent remainder (dtAccum < kTimeTick) at its current velocity. When the
		// frame rate outruns the tick rate (>60 fps) the accumulator only fires a tick
		// every 2-3 frames, so without this the rendered offset would freeze then jump
		// — a tick-rate sawtooth judder, and a framerate-dependent one (absent at <=60
		// fps where dtAccum stays ~0). This is the standard fixed-timestep render
		// interpolation, done as a cheap forward extrapolation: output-only, never fed
		// back into the sim, so it cannot affect stability.
		const RE::NiPoint3 renderPos = a_state.simPos + a_state.vel * a_state.dtAccum;

		// Lag of the mass behind rest, brought back into the bone's local frame.
		const RE::NiPoint3 dispWorld = renderPos - restWorld;
		RE::NiPoint3 dispLocal = Math::TransposeMul(a_parentWorldRot, dispWorld);

		Math::ClampAbs(dispLocal.x, a_params.maxOffset.x);
		Math::ClampAbs(dispLocal.y, a_params.maxOffset.y);
		Math::ClampAbs(dispLocal.z, a_params.maxOffset.z);
		return dispLocal;
	}
}
