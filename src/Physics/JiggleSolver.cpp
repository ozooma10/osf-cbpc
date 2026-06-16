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
			a_state.prevTickTarget = restWorld;
			a_state.tickDisp = {};
			a_state.prevTickDisp = {};
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
			a_state.prevTickTarget = restWorld;
			a_state.tickDisp = {};
			a_state.prevTickDisp = {};
			a_state.dtAccum = 0.0f;
			return {};
		}
		// Bracket for the per-tick target interpolation below (last frame's rest ->
		// this frame's). prevRestWorld also feeds the teleport guard above.
		const RE::NiPoint3 prevRest = a_state.prevRestWorld;
		a_state.prevRestWorld = restWorld;
		const float invMass = (a_params.mass > 1e-4f) ? (1.0f / a_params.mass) : 0.0f;

		// Fixed-timestep accumulator: bank this frame's dt and integrate as many whole
		// kTimeTick sub-steps as it now affords, carrying the remainder. The integration
		// dt is therefore constant (kTimeTick) no matter the frame rate, which is what
		// makes the tuning framerate-independent. Cap the per-frame step count and drop
		// the backlog on overflow so a low-fps spike can't spiral.
		const float dtPrev = a_state.dtAccum;  // how far the sim already lags render this frame
		a_state.dtAccum += a_dt;
		int steps = static_cast<int>(a_state.dtAccum / kTimeTick);  // floor; dtAccum >= 0
		if (steps > kMaxSubSteps) {
			steps = kMaxSubSteps;
			a_state.dtAccum = 0.0f;  // discard time we will never catch up on
		} else {
			a_state.dtAccum -= static_cast<float>(steps) * kTimeTick;
		}

		if (steps > 0) {
			// Place each tick's target at its true sim-time, interpolated between last
			// frame's rest (prevRest) and this frame's (restWorld). The sim lags render by
			// dtPrev at frame start, so tick k lands at fraction (k*tick - dtPrev)/dt
			// through the frame (always in (0,1] — a tick only fires once the accumulator
			// is near-full). This is what makes high fps correct: when the frame rate
			// outruns the tick rate, ticks fire irregularly (every 2-3 frames) and the
			// fraction still pins each one to the right instant, so the spring sees a
			// smooth drive. A fixed per-tick position increment would instead lurch the
			// target +/-25% per tick at >60 fps. At <=60 fps dtPrev~0 and the fractions
			// are evenly spaced — identical to a plain per-frame sweep.
			const RE::NiPoint3 restSpan = restWorld - prevRest;
			const float invFrameDt = 1.0f / a_dt;

			// Spring toward the interpolated target; the velocity feed-forward
			// (vel - restVel) makes the mass track steady locomotion with NO sustained
			// lag, so jiggle is purely transient — reacts to acceleration (footsteps,
			// start/stop), returns to rest, like the native ragdoll constraint.
			// accel = (k*(rest - sim) - c*(vel - restVel))/mass + g, per local axis;
			// relative damping stays INSIDE the loop (NOT CBPC's absolute damping).
			// maxOffset (clamped at the end) plays the role of the cone/twist limit.
			//
			// restVel is the target's velocity over THIS tick (target - previous tick's
			// target) / tick — measured on the fixed tick grid, so it is identical at any
			// frame rate. Deriving it from the frame delta instead ((rest - prevRest)/dt)
			// silently makes the feed-forward framerate-dependent: at >60 fps the frame
			// window is a misaligned sub-sample of the tick interval, which over-damps and
			// phase-shifts the jiggle vs 30/60 fps (where the windows happen to align).
			for (int k = 1; k <= steps; ++k) {
				const float frac = (static_cast<float>(k) * kTimeTick - dtPrev) * invFrameDt;
				const RE::NiPoint3 target = prevRest + restSpan * frac;
				const RE::NiPoint3 restVel = (target - a_state.prevTickTarget) * (1.0f / kTimeTick);
				const RE::NiPoint3 toRest = target - a_state.simPos;
				const RE::NiPoint3 accel{
					(a_params.stiffness.x * toRest.x - a_params.damping.x * (a_state.vel.x - restVel.x)) * invMass + a_params.gravity.x,
					(a_params.stiffness.y * toRest.y - a_params.damping.y * (a_state.vel.y - restVel.y)) * invMass + a_params.gravity.y,
					(a_params.stiffness.z * toRest.z - a_params.damping.z * (a_state.vel.z - restVel.z)) * invMass + a_params.gravity.z
				};

				// Semi-implicit Euler at the fixed sub-step dt.
				a_state.vel = a_state.vel + accel * kTimeTick;
				a_state.simPos = a_state.simPos + a_state.vel * kTimeTick;

				// Advance the tick references: this tick's target becomes the next tick's
				// velocity baseline, and record this tick's lag (relative to ITS target, so
				// locomotion cancels) for the output interpolation below.
				a_state.prevTickTarget = target;
				a_state.prevTickDisp = a_state.tickDisp;
				a_state.tickDisp = a_state.simPos - target;
			}
		}

		// Output: interpolate between the two most recent committed tick lags by the
		// unspent-remainder fraction (alpha = dtAccum / kTimeTick, in [0,1)). This is the
		// standard fixed-timestep render interpolation — bounded (never overshoots) and a
		// CONSISTENT ~1-tick lag at every frame rate, which is what keeps 144 fps in step
		// with 30/60 (where alpha~0, so the output is simply the latest tick's lag). It
		// interpolates the LAG, not simPos, so locomotion cancels and there's no
		// velocity-dependent bias. Without it, >60 fps shows a tick-rate sawtooth judder
		// (the sim only advances every 2-3 frames). The committed ticks are untouched, so
		// the tuning stays framerate-independent.
		const float alpha = a_state.dtAccum * (1.0f / kTimeTick);
		const RE::NiPoint3 dispWorld =
			a_state.prevTickDisp + (a_state.tickDisp - a_state.prevTickDisp) * alpha;
		RE::NiPoint3 dispLocal = Math::TransposeMul(a_parentWorldRot, dispWorld);

		Math::ClampAbs(dispLocal.x, a_params.maxOffset.x);
		Math::ClampAbs(dispLocal.y, a_params.maxOffset.y);
		Math::ClampAbs(dispLocal.z, a_params.maxOffset.z);
		return dispLocal;
	}
}
