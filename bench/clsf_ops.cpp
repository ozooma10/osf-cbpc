// Out-of-line CLSF vector operators used by the solver, reproduced VERBATIM
// from lib/commonlibsf/src/RE/N/{NiPoint,NiMatrix3}.cpp.
//
// Why copied instead of linking the real CLSF .cpp: those translation units also
// define functions that bind to game addresses (e.g. NiMatrix3::ToEulerAnglesXYZ
// via REL::Relocation) and use MSVC-only spellings (std::sqrtf), so they cannot
// be compiled off-target. The operators the solver actually calls are pure and
// reproduced here byte-for-byte, in a SEPARATE translation unit from
// JiggleSolver.cpp — which is exactly the property that matters: without LTO the
// compiler cannot inline these into StepBone (same as the shipping plugin); with
// -flto / MSVC /GL it can. So the harness measures the real inlining/codegen
// delta, not a shimmed approximation.

#include "RE/N/NiMatrix3.h"
#include "RE/N/NiPoint.h"

namespace RE
{
	NiPoint3::NiPoint3(float a_x, float a_y, float a_z) noexcept :
		x(a_x), y(a_y), z(a_z)
	{}

	NiPoint3 NiPoint3::operator+(const NiPoint3& a_rhs) const noexcept
	{
		return NiPoint3(x + a_rhs.x, y + a_rhs.y, z + a_rhs.z);
	}

	NiPoint3 NiPoint3::operator-(const NiPoint3& a_rhs) const noexcept
	{
		return NiPoint3(x - a_rhs.x, y - a_rhs.y, z - a_rhs.z);
	}

	NiPoint3 NiPoint3::operator*(float a_scalar) const noexcept
	{
		return NiPoint3(x * a_scalar, y * a_scalar, z * a_scalar);
	}

	float NiPoint3::GetSquaredDistance(const NiPoint3& a_point) const noexcept
	{
		const float dx = a_point.x - x;
		const float dy = a_point.y - y;
		const float dz = a_point.z - z;
		return dx * dx + dy * dy + dz * dz;
	}

	NiPoint4::NiPoint4(float a_x, float a_y, float a_z, float a_w) noexcept :
		x(a_x), y(a_y), z(a_z), w(a_w)
	{}

	NiMatrix3::NiMatrix3(
		float a_x0, float a_y0, float a_z0, float a_w0,
		float a_x1, float a_y1, float a_z1, float a_w1,
		float a_x2, float a_y2, float a_z2, float a_w2) noexcept
	{
		entry[0] = { a_x0, a_y0, a_z0, a_w0 };
		entry[1] = { a_x1, a_y1, a_z1, a_w1 };
		entry[2] = { a_x2, a_y2, a_z2, a_w2 };
	}

	// Equivalent to the CLSF body (entry[r][c] -> entry[r].{x,y,z}); avoids
	// pulling in NiPoint4::operator[]. Same arithmetic, same out-of-line codegen.
	NiPoint3 NiMatrix3::operator*(const NiPoint3& a_rhs) const noexcept
	{
		return NiPoint3(
			entry[0].x * a_rhs.x + entry[0].y * a_rhs.y + entry[0].z * a_rhs.z,
			entry[1].x * a_rhs.x + entry[1].y * a_rhs.y + entry[1].z * a_rhs.z,
			entry[2].x * a_rhs.x + entry[2].y * a_rhs.y + entry[2].z * a_rhs.z);
	}
}
