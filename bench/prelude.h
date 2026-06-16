#pragma once

// Offline-benchmark prelude.
//
// The CLSF Ni* headers (RE/N/NiPoint.h, NiMatrix3.h, NiTransform.h) and their
// .cpp implementations rely on the SFSE precompiled header (SFSE/Impl/PCH.h) to
// have already pulled in the standard library. The benchmark compiles those
// real translation units WITHOUT the full SFSE toolchain, so we force-include
// (via `-include bench/prelude.h`) the exact subset they use. This lets the
// harness measure the REAL solver against the REAL out-of-line CLSF operators —
// which is what makes the LTO / inlining / SIMD codegen deltas observable.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <numbers>
#include <tuple>
#include <type_traits>
