#pragma once

// Small math helpers over the CLSF Ni* types for the jiggle solver. Kept
// dependency-light so the solver (JiggleSolver) can be unit-tested offline
// against shimmed Ni* types (see test/SolverTest.cpp).

#include "RE/N/NiMatrix3.h"
#include "RE/N/NiPoint.h"

namespace OSF::Physics::Math
{
	// Rotate a WORLD-space vector into the LOCAL frame defined by an orthonormal
	// world rotation: result = R^T * v. CLSF NiMatrix3 stores three ROWS
	// (entry[0..2]), so R*v = (entry[r] . v); the transpose multiply below dots
	// against the COLUMNS. (Verify entry[] member layout against the CLSF fork if
	// rotations ever look mirrored — see docs/RE.md.)
	inline RE::NiPoint3 TransposeMul(const RE::NiMatrix3& a_m, const RE::NiPoint3& a_v)
	{
		return RE::NiPoint3{
			a_m.entry[0].x * a_v.x + a_m.entry[1].x * a_v.y + a_m.entry[2].x * a_v.z,
			a_m.entry[0].y * a_v.x + a_m.entry[1].y * a_v.y + a_m.entry[2].y * a_v.z,
			a_m.entry[0].z * a_v.x + a_m.entry[1].z * a_v.y + a_m.entry[2].z * a_v.z
		};
	}

	// Symmetric per-axis clamp to [-a_limit, +a_limit].
	inline void ClampAbs(float& a_value, float a_limit)
	{
		if (a_value > a_limit) {
			a_value = a_limit;
		} else if (a_value < -a_limit) {
			a_value = -a_limit;
		}
	}
}
