// OSF CBPC — framerate-consistency gate (solver correctness, not perf).
//
// Drives the real JiggleSolver::StepBone over one continuous trajectory at 30/60/144
// fps and asserts the resulting jiggle displacement curves agree within tolerance —
// i.e. the tuning is framerate-independent (the P1 fixed-timestep goal). Prints a
// table and exits non-zero on failure, so CI can gate on it (see bench/README.md and
// .github/workflows/bench.yml). The math lives in bench/fps_consistency.h.

#include "bench/fps_consistency.h"

#include <cmath>
#include <cstdio>

namespace
{
	// Tuning under test: the player profile's snappiest bone (high stiffness) is the
	// hardest to keep stable across frame rates, so exercise that. maxOffset is left
	// generous so the clamp can't mask a divergence we're trying to detect.
	OSF::Physics::BoneParams MakeParams()
	{
		OSF::Physics::BoneParams p;
		p.stiffness = { 450.0f, 450.0f, 450.0f };
		p.damping = { 28.0f, 28.0f, 28.0f };
		p.gravity = { 0.0f, 0.0f, 0.0f };
		p.maxOffset = { 8.0f, 8.0f, 8.0f };
		p.mass = 1.0f;
		return p;
	}

	void PrintRow(const char* a_label, const bench::fps::Divergence& a_d, double a_tol, bool a_gated)
	{
		const char* mark = !a_gated ? "    " : (a_d.rel <= a_tol ? " ok " : "FAIL");
		std::printf("  %-26s  max-diff %7.4f u   peak %7.4f u   rel %6.2f%%   %s\n",
			a_label, a_d.maxAbs, a_d.peak, 100.0 * a_d.rel, mark);
	}
}

int main()
{
	using namespace bench::fps;

	constexpr double T = 3.0;            // seconds simulated
	constexpr double t0 = 0.5;           // skip the prime/settle transient before comparing
	constexpr int    kCheckpoints = 512; // common wall-clock sample points in [t0, T]
	constexpr double kTol = 0.10;        // pass if cross-rate error <= 10% of peak displacement
	constexpr double kTeethFactor = 2.0; // the single-step yardstick must be >=2x worse

	// ~20deg about Z (matches solver_bench.h) so the world->local transform is on the
	// non-identity path; a non-trivial bone offset so rotation actually matters.
	const float ang = 0.35f;
	const float cs = std::cos(ang);
	const float sn = std::sin(ang);
	const RE::NiMatrix3 rot(cs, -sn, 0.0f, 0.0f, sn, cs, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	const RE::NiPoint3 boneLocalRest{ 2.0f, 0.5f, -1.0f };
	const OSF::Physics::BoneParams params = MakeParams();

	// Fixed-step solver (the code under test) at each frame rate.
	const Curve f30 = RunFixedStep(30.0, T, params, rot, boneLocalRest);
	const Curve f60 = RunFixedStep(60.0, T, params, rot, boneLocalRest);
	const Curve f144 = RunFixedStep(144.0, T, params, rot, boneLocalRest);

	const Divergence d_30_60 = CompareCurves(f60, f30, t0, T, kCheckpoints);
	const Divergence d_60_144 = CompareCurves(f60, f144, t0, T, kCheckpoints);
	const Divergence d_30_144 = CompareCurves(f30, f144, t0, T, kCheckpoints);

	// Yardstick: the pre-normalization single-step integrator over the same drive.
	const Curve r30 = RunSingleEuler(30.0, T, params, rot, boneLocalRest);
	const Curve r144 = RunSingleEuler(144.0, T, params, rot, boneLocalRest);
	const Divergence ref_30_144 = CompareCurves(r30, r144, t0, T, kCheckpoints);

	std::printf("OSF CBPC — framerate-consistency check\n");
	std::printf("trajectory %.1fs, compared over [%.2fs, %.1fs] at %d checkpoints, tol %.0f%%\n\n",
		T, t0, T, kCheckpoints, 100.0 * kTol);

	std::printf("fixed-step solver (under test):\n");
	PrintRow("30 vs 60 fps", d_30_60, kTol, true);
	PrintRow("60 vs 144 fps", d_60_144, kTol, true);
	PrintRow("30 vs 144 fps", d_30_144, kTol, true);

	std::printf("\nsingle-step Euler (pre-normalization yardstick):\n");
	PrintRow("30 vs 144 fps", ref_30_144, kTol, false);

	const double worst = std::max({ d_30_60.rel, d_60_144.rel, d_30_144.rel });
	const bool within = worst <= kTol;
	// The gate must have teeth: the trajectory has to be demanding enough that the
	// single-step scheme actually diverges, else "fixed-step passes" proves nothing.
	const bool hasTeeth = ref_30_144.rel > d_30_144.rel * kTeethFactor;

	std::printf("\nfixed-step worst cross-rate error: %.2f%%  (tol %.0f%%)\n", 100.0 * worst, 100.0 * kTol);
	std::printf("single-step 30-vs-144 error:       %.2f%%  -> fixed-step is %.1fx tighter\n",
		100.0 * ref_30_144.rel, d_30_144.rel > 0.0 ? ref_30_144.rel / d_30_144.rel : 0.0);

	if (within && hasTeeth) {
		std::printf("\nPASS — jiggle is framerate-independent within tolerance.\n");
		return 0;
	}
	if (!within) {
		std::printf("\nFAIL — fixed-step curves diverge across frame rates beyond tolerance.\n");
	}
	if (!hasTeeth) {
		std::printf("\nFAIL — yardstick too close to the solver; the trajectory is not demanding "
					"enough for this check to mean anything.\n");
	}
	return 1;
}
