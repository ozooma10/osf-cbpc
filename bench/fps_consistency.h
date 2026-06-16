#pragma once

// Framerate-consistency validation for the jiggle solver (the P1 timestep work).
//
// JiggleSolver::StepBone integrates the spring in fixed kTimeTick sub-steps with a
// per-bone accumulator, so the SAME tuning must produce the SAME jiggle whether the
// game renders at 30, 60, or 144 fps. This harness proves that end-to-end: it drives
// the REAL StepBone over one continuous, jiggle-exciting parent trajectory, sampled
// once per frame at each frame rate (exactly as the plugin samples the parent bone),
// then compares the resulting local-displacement curves at common wall-clock
// checkpoints.
//
// The pre-normalization integrator — a single variable-dt Euler step — is reproduced
// here verbatim as the regression yardstick. Its error (and its effective per-step
// damping) scales with dt, so its 30-vs-144 curves diverge badly. The test asserts
// the fixed-step solver stays within tolerance AND that it is dramatically more
// stable than the single-step scheme (so the gate can never silently go vacuous).
//
// Note this measures the *whole* framerate story: the integrator (which the fixed
// sub-step makes rate-independent) plus the forcing, which is still sampled once per
// frame. The forcing-sampling difference between rates is small for a realistic
// trajectory and is the irreducible floor; the integrator difference is what the
// single-step scheme adds on top and what this change removes.

#include "Physics/JiggleSolver.h"
#include "Util/Math.h"

#include "RE/N/NiMatrix3.h"
#include "RE/N/NiPoint.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bench::fps
{
	// A continuous parent-bone world trajectory rich enough to excite the spring on
	// all three axes: a lateral sway + fore/aft surge + a vertical "footstep" bounce,
	// each a smooth sinusoid at a locomotion-realistic frequency (<=1.6 Hz). The
	// frequencies are deliberately well under the 30 fps Nyquist (15 Hz) and the
	// signal is cusp-free, so the once-per-frame parent sampling captures it almost
	// identically at every rate. What's left for the solver to get wrong is the
	// integration — which is exactly what the fixed sub-step is meant to make
	// rate-independent. (An aggressive, cusp-laden trajectory would alias differently
	// per rate no matter how perfect the integrator, so it would test the wrong thing.)
	inline RE::NiPoint3 ParentTrajectory(double a_t)
	{
		constexpr double kTwoPi = 6.283185307179586;
		const double sway = 3.0 * std::sin(kTwoPi * 0.8 * a_t);          // ~0.8 Hz lateral
		const double surge = 2.0 * std::sin(kTwoPi * 1.2 * a_t + 0.7);   // ~1.2 Hz fore/aft
		const double bounce = 1.6 * std::sin(kTwoPi * 1.6 * a_t);        // ~1.6 Hz bounce
		return RE::NiPoint3{
			static_cast<float>(64.0 + sway),
			static_cast<float>(surge),
			static_cast<float>(96.0 + bounce)
		};
	}

	struct Sample
	{
		double       t;
		RE::NiPoint3 disp;
	};
	using Curve = std::vector<Sample>;

	// --- the regression yardstick: the pre-normalization integrator ---------------
	//
	// ONE semi-implicit Euler step at the raw frame dt — what the fixed-step
	// accumulator replaced. Kept ONLY so the test can show the framerate dependence
	// it fixes. Mirrors StepBone's spring math exactly, minus the sub-step loop.
	struct RefState
	{
		RE::NiPoint3 simPos{};
		RE::NiPoint3 vel{};
		RE::NiPoint3 prevRest{};
		bool         primed{ false };
	};

	inline RE::NiPoint3 StepRefSingleEuler(RefState& a_s, const OSF::Physics::BoneParams& a_p,
		const RE::NiPoint3& a_parentPos, const RE::NiMatrix3& a_parentRot,
		const RE::NiPoint3& a_boneLocalRest, float a_dt)
	{
		const RE::NiPoint3 restWorld = a_parentPos + a_parentRot * a_boneLocalRest;
		if (!a_s.primed) {
			a_s.simPos = restWorld;
			a_s.vel = {};
			a_s.prevRest = restWorld;
			a_s.primed = true;
			return {};
		}
		const float invDt = 1.0f / a_dt;
		const RE::NiPoint3 restVel = (restWorld - a_s.prevRest) * invDt;
		a_s.prevRest = restWorld;
		const RE::NiPoint3 toRest = restWorld - a_s.simPos;
		const float invMass = (a_p.mass > 1e-4f) ? (1.0f / a_p.mass) : 0.0f;
		const RE::NiPoint3 accel{
			(a_p.stiffness.x * toRest.x - a_p.damping.x * (a_s.vel.x - restVel.x)) * invMass + a_p.gravity.x,
			(a_p.stiffness.y * toRest.y - a_p.damping.y * (a_s.vel.y - restVel.y)) * invMass + a_p.gravity.y,
			(a_p.stiffness.z * toRest.z - a_p.damping.z * (a_s.vel.z - restVel.z)) * invMass + a_p.gravity.z
		};
		a_s.vel = a_s.vel + accel * a_dt;
		a_s.simPos = a_s.simPos + a_s.vel * a_dt;
		RE::NiPoint3 disp = OSF::Physics::Math::TransposeMul(a_parentRot, a_s.simPos - restWorld);
		OSF::Physics::Math::ClampAbs(disp.x, a_p.maxOffset.x);
		OSF::Physics::Math::ClampAbs(disp.y, a_p.maxOffset.y);
		OSF::Physics::Math::ClampAbs(disp.z, a_p.maxOffset.z);
		return disp;
	}

	// --- drivers ------------------------------------------------------------------

	// Drive the REAL fixed-step solver over [0, a_T] seconds at a_fps; record the
	// returned local displacement at every frame.
	inline Curve RunFixedStep(double a_fps, double a_T, const OSF::Physics::BoneParams& a_params,
		const RE::NiMatrix3& a_rot, const RE::NiPoint3& a_boneLocalRest)
	{
		OSF::Physics::BoneState state{};
		const float dt = static_cast<float>(1.0 / a_fps);
		const std::size_t frames = static_cast<std::size_t>(a_T * a_fps + 0.5);
		Curve out;
		out.reserve(frames + 1);
		for (std::size_t f = 0; f <= frames; ++f) {
			const double t = static_cast<double>(f) / a_fps;
			const RE::NiPoint3 disp = OSF::Physics::StepBone(
				state, a_params, ParentTrajectory(t), a_rot, a_boneLocalRest, dt);
			out.push_back({ t, disp });
		}
		return out;
	}

	// Same drive, but through the single-step yardstick integrator.
	inline Curve RunSingleEuler(double a_fps, double a_T, const OSF::Physics::BoneParams& a_params,
		const RE::NiMatrix3& a_rot, const RE::NiPoint3& a_boneLocalRest)
	{
		RefState state{};
		const float dt = static_cast<float>(1.0 / a_fps);
		const std::size_t frames = static_cast<std::size_t>(a_T * a_fps + 0.5);
		Curve out;
		out.reserve(frames + 1);
		for (std::size_t f = 0; f <= frames; ++f) {
			const double t = static_cast<double>(f) / a_fps;
			const RE::NiPoint3 disp = StepRefSingleEuler(
				state, a_params, ParentTrajectory(t), a_rot, a_boneLocalRest, dt);
			out.push_back({ t, disp });
		}
		return out;
	}

	// --- comparison ---------------------------------------------------------------

	// Linear interpolation of a (time-monotonic) curve at arbitrary t.
	inline RE::NiPoint3 SampleAt(const Curve& a_c, double a_t)
	{
		if (a_c.empty()) {
			return {};
		}
		if (a_t <= a_c.front().t) {
			return a_c.front().disp;
		}
		if (a_t >= a_c.back().t) {
			return a_c.back().disp;
		}
		std::size_t lo = 0, hi = a_c.size() - 1;
		while (hi - lo > 1) {
			const std::size_t mid = (lo + hi) / 2;
			if (a_c[mid].t <= a_t) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		const double span = a_c[hi].t - a_c[lo].t;
		const float u = span > 0.0 ? static_cast<float>((a_t - a_c[lo].t) / span) : 0.0f;
		const RE::NiPoint3& A = a_c[lo].disp;
		const RE::NiPoint3& B = a_c[hi].disp;
		return RE::NiPoint3{ A.x + (B.x - A.x) * u, A.y + (B.y - A.y) * u, A.z + (B.z - A.z) * u };
	}

	struct Divergence
	{
		double maxAbs{ 0.0 };  // worst per-axis |dispA - dispB| over the checkpoints (game units)
		double peak{ 0.0 };    // peak |disp| seen on either curve (signal amplitude)
		double rel{ 0.0 };     // maxAbs / peak — the framerate-consistency error
	};

	// Worst per-axis displacement difference between two curves over `a_checkpoints`
	// evenly spaced wall-clock samples in [a_t0, a_T], normalized by the signal's peak
	// amplitude so the result is a relative error independent of trajectory scale.
	inline Divergence CompareCurves(const Curve& a_a, const Curve& a_b,
		double a_t0, double a_T, int a_checkpoints)
	{
		Divergence d;
		double peak = 1e-9;
		for (int i = 0; i <= a_checkpoints; ++i) {
			const double t = a_t0 + (a_T - a_t0) * (static_cast<double>(i) / a_checkpoints);
			const RE::NiPoint3 da = SampleAt(a_a, t);
			const RE::NiPoint3 db = SampleAt(a_b, t);
			d.maxAbs = std::max({ d.maxAbs,
				std::fabs(static_cast<double>(da.x - db.x)),
				std::fabs(static_cast<double>(da.y - db.y)),
				std::fabs(static_cast<double>(da.z - db.z)) });
			peak = std::max({ peak,
				std::fabs(static_cast<double>(da.x)), std::fabs(static_cast<double>(da.y)), std::fabs(static_cast<double>(da.z)),
				std::fabs(static_cast<double>(db.x)), std::fabs(static_cast<double>(db.y)), std::fabs(static_cast<double>(db.z)) });
		}
		d.peak = peak;
		d.rel = d.maxAbs / peak;
		return d;
	}
}
