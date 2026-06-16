#pragma once

// Solver microbenchmark (cost regime B: per-tracked-bone work).
//
// Drives the REAL JiggleSolver::StepBone over the REAL out-of-line CLSF NiPoint3
// / NiMatrix3 operators, in a loop shaped exactly like JiggleActor::Update:
//
//     rest = local[rigIndex].translate;            // scattered read
//     disp = StepBone(state, params, parentPos, parentRot, rest, dt);
//     local[rigIndex].translate = rest + disp;     // scattered write
//
// so the number it reports is representative of the plugin's hot inner loop,
// including the operator-call ABI traffic that LTO removes and the SIMD rewrite
// collapses. The driving motion is a precomputed walk-cycle table so the springs
// actually integrate (rest position keeps moving) without the driver's own math
// polluting the timing.

#include "Physics/JiggleSolver.h"
#include "RE/N/NiTransform.h"

#include "bench/bench_util.h"

#include <cstdint>
#include <vector>

namespace bench
{
	class SolverWorkload
	{
	public:
		// a_bones: total springs stepped per frame (5 = player profile; pass e.g.
		//          400 to model an M2 crowd of ~80 actors x 5 bones).
		// a_scatter: if true, rig indices are shuffled so local[] writes miss cache
		//            the way real rig indices do; if false, sequential (best case).
		SolverWorkload(std::size_t a_bones, bool a_scatter)
		{
			using namespace OSF::Physics;

			// Fixed non-identity parent rotation (~20 deg about Z): exercises the
			// real NiMatrix3 * NiPoint3 and TransposeMul cost, not an identity path.
			const float a = 0.35f;
			const float cs = std::cos(a);
			const float sn = std::sin(a);
			rot = RE::NiMatrix3(
				cs, -sn, 0.0f, 0.0f,
				sn, cs, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f);

			// Walk-cycle motion table (256 frames): a looping translation so the
			// rest world position keeps accelerating -> springs stay excited.
			motion.resize(kMotionMask + 1);
			for (std::size_t i = 0; i <= kMotionMask; ++i) {
				const float t = static_cast<float>(i) * (6.2831853f / (kMotionMask + 1));
				motion[i] = RE::NiPoint3{ 2.0f * std::sin(t), 1.0f * std::sin(2.0f * t), 0.5f * std::cos(t) };
			}

			// One rig slot per bone, plus headroom so scattered indices spread over
			// a realistic skeleton-sized buffer (~300 NiTransforms = ~19 KB).
			const std::size_t slots = a_bones < 300 ? 300 : a_bones + 16;
			local.resize(slots);

			params.resize(a_bones);
			state.resize(a_bones);
			rigIndex.resize(a_bones);
			parentBase.resize(a_bones);

			for (std::size_t b = 0; b < a_bones; ++b) {
				// Mix of the profile's actual tunings so codegen sees varied data.
				BoneParams p;
				const float k = (b % 3 == 0) ? 450.0f : (b % 3 == 1) ? 120.0f : 500.0f;
				const float c = (b % 3 == 0) ? 28.0f : (b % 3 == 1) ? 12.0f : 28.0f;
				p.stiffness = { k, k, k };
				p.damping = { c, c, c };
				p.gravity = { 0.0f, 0.0f, 0.0f };
				p.maxOffset = { 0.6f, 0.6f, 0.7f };
				p.mass = 1.0f;
				params[b] = p;

				parentBase[b] = RE::NiPoint3{
					static_cast<float>(b) * 3.0f, static_cast<float>(b) * 1.5f, 64.0f
				};
				rigIndex[b] = a_scatter
					? static_cast<std::uint32_t>((b * 1664525u + 1013904223u) % slots)
					: static_cast<std::uint32_t>(b);
				local[rigIndex[b]].translate = RE::NiPoint3{ 0.1f, -0.2f, 0.05f };
			}
		}

		// Advance one frame over every bone. Mirrors JiggleActor::Update exactly.
		double StepOneFrame()
		{
			using namespace OSF::Physics;
			const RE::NiPoint3& m = motion[frame & kMotionMask];
			double cks = 0.0;
			for (std::size_t b = 0; b < params.size(); ++b) {
				const RE::NiPoint3 parentPos = parentBase[b] + m;
				const RE::NiPoint3 rest = local[rigIndex[b]].translate;
				const RE::NiPoint3 disp =
					StepBone(state[b], params[b], parentPos, rot, rest, kDt);
				const RE::NiPoint3 next = rest + disp;
				local[rigIndex[b]].translate = next;
				cks += next.x + next.y + next.z;
			}
			++frame;
			return cks;
		}

		std::size_t Bones() const { return params.size(); }

	private:
		static constexpr std::size_t kMotionMask = 255;
		static constexpr float       kDt = 1.0f / 60.0f;

		RE::NiMatrix3                          rot{};
		std::vector<RE::NiPoint3>              motion;
		std::vector<RE::NiTransform>           local;       // the flat rig buffer
		std::vector<OSF::Physics::BoneParams>  params;
		std::vector<OSF::Physics::BoneState>   state;
		std::vector<std::uint32_t>             rigIndex;
		std::vector<RE::NiPoint3>              parentBase;
		std::uint64_t                          frame{ 0 };
	};

	inline Stats RunSolverBench(const char* a_label, std::size_t a_bones, bool a_scatter,
		std::size_t a_batches, std::size_t a_innerFrames)
	{
		auto wl = std::make_shared<SolverWorkload>(a_bones, a_scatter);
		Stats s = TimeOp(a_label, a_batches, a_innerFrames, wl->Bones(),
			[wl]() { return wl->StepOneFrame(); });
		s.unit = "ns/bone";
		return s;
	}
}
