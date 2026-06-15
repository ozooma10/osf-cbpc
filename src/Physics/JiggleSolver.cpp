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
			a_state.primed = true;
			return {};
		}

		// Teleport / cell-load guard: a huge one-frame jump of the rest position
		// is not motion — re-seat the mass so the spring doesn't fling it.
		if (restWorld.GetSquaredDistance(a_state.prevRestWorld) > kTeleportSq) {
			a_state.simPos = restWorld;
			a_state.vel = {};
			a_state.prevRestWorld = restWorld;
			return {};
		}
		// Velocity of the rest target this frame (for relative damping below).
		const float invDt = 1.0f / a_dt;
		const RE::NiPoint3 restVel = (restWorld - a_state.prevRestWorld) * invDt;
		a_state.prevRestWorld = restWorld;

		// Spring toward rest; damp the mass's velocity RELATIVE to the rest
		// target's velocity (vel - restVel). This velocity feed-forward makes the
		// mass track steady locomotion with NO sustained lag, so jiggle is purely
		// transient — it reacts to acceleration (footsteps, start/stop) and returns
		// to rest, like the native ragdoll constraint. The maxOffset clamp plays the
		// role of the cone/twist limit. accel = (k*(rest-sim) - c*(vel-restVel))/mass + g.
		const RE::NiPoint3 toRest = restWorld - a_state.simPos;
		const float invMass = (a_params.mass > 1e-4f) ? (1.0f / a_params.mass) : 0.0f;
		const RE::NiPoint3 accel{
			(a_params.stiffness.x * toRest.x - a_params.damping.x * (a_state.vel.x - restVel.x)) * invMass + a_params.gravity.x,
			(a_params.stiffness.y * toRest.y - a_params.damping.y * (a_state.vel.y - restVel.y)) * invMass + a_params.gravity.y,
			(a_params.stiffness.z * toRest.z - a_params.damping.z * (a_state.vel.z - restVel.z)) * invMass + a_params.gravity.z
		};

		// Semi-implicit Euler.
		a_state.vel = a_state.vel + accel * a_dt;
		a_state.simPos = a_state.simPos + a_state.vel * a_dt;

		// Lag of the mass behind rest, brought back into the bone's local frame.
		const RE::NiPoint3 dispWorld = a_state.simPos - restWorld;
		RE::NiPoint3 dispLocal = Math::TransposeMul(a_parentWorldRot, dispWorld);

		Math::ClampAbs(dispLocal.x, a_params.maxOffset.x);
		Math::ClampAbs(dispLocal.y, a_params.maxOffset.y);
		Math::ClampAbs(dispLocal.z, a_params.maxOffset.z);
		return dispLocal;
	}
}
