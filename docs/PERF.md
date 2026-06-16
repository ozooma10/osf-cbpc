# OSF CBPC — performance model & optimization roadmap

The reference the `bench/` harness is built around. `bench/` measures the two
cost regimes below so each optimization can be validated before/after. This doc
is the *why*; `bench/README.md` is the *how*.

## The cost model (the one thing to internalize)

`BGSModelNode::Update` (the slot-2 hook) fires **once per skeleton, per frame, on
multiple render threads** — not once for the player. So cost splits in two:

| Regime | Frequency | Who pays | Bench |
|---|---|---|---|
| **A — per-skeleton tax** | `#skeletons × fps × threads` | *every* rigged thing in the scene | `registry/*` |
| **B — per-tracked-bone work** | `#tracked actors × #bones × fps` | player today; ~5–20 actors in M2 | `solver/*` |

In a quiet interior, B dominates and the plugin is ~free. In a populated city,
**A dominates by 1–2 orders of magnitude** and does no useful work for ~99% of
calls (those skeletons aren't tracked). Optimize A first.

## Framerate normalization (fixed timestep)

`StepBone` integrates the spring in fixed `kTimeTick` (1/60 s) sub-steps via a
per-bone accumulator, not one variable-dt Euler step — so the same tuning behaves
identically at 30/60/144 fps (a single Euler step's error and effective damping
scale with dt). This has a regime-B cost shape: per-frame solver work now scales
with the sub-step count ≈ `dt × 60`, capped at `kMaxSubSteps`. So **≥60 fps costs
≤1 step/frame** (often 0 at 144 fps, since the accumulator only fires a tick every
~2–3 frames — *cheaper* than before), while **30 fps costs ~2 steps/frame** (~2× the
integration work, paid only by the few tracked actors in regime B — negligible at
player-only M1 scale, worth watching at M2 crowd scale on a 30 fps machine). On top
of the per-step work there is **fixed per-call overhead**: a tick-interval feed-forward
velocity and a bounded output interpolation between the two most recent tick lags
(standard fixed-timestep render interpolation — it removes the >60 fps judder and is
what makes high fps actually match low fps). `BoneState` grew by three `NiPoint3`
(prev-tick target + the two interpolated lags) and the accumulator. In the no-LTO
`solver/*` bench (`dt = 1/60`, one sub-step) this lands around +40% ns/bone vs the
plain Euler step — sub-microsecond for the player, and recoverable by Tier 3.2's
`__m128` rewrite (the inner sub-step body and the output blend are both branch-free
3-axis math). Correctness (the curves actually matching across rates — ~1.8% worst
case vs the unnormalized 92%) is gated offline by `bench/fps_test.cpp` — see
Validation below.

## Findings, ranked by ROI

### Tier 1 — build/codegen flags (trivial, no behavior change)
- **1.1 Enable LTO.** The CLSF `NiPoint3`/`NiMatrix3` operators are out-of-line
  in a static lib; without LTO they can't inline into `StepBone` (and a 12-byte
  `NiPoint3` returns via memory on the Win64 ABI). `set_policy("build.optimization.lto", true)`.
- **1.2 `/arch:AVX2`.** Free — Starfield refuses to launch without AVX2, so every
  real player's CPU has it. Enables FMA3 + VEX.
- **1.3 `/fp:contract`** (or scoped `/fp:fast` in the solver TU). Lets the spring
  math fuse multiply-adds. Jiggle has no precision requirements.
- **1.4** Ship `mode.release`, not `releasedbg`.
- Measure with: `solver/*` across `make CONFIG=release|lto|avx2`.

### Tier 2 — kill the per-skeleton tax (the real bottleneck at scale)
- **2.1 Stop reading the wall clock every call.** `MaybeRefresh()` calls
  `steady_clock::now()` + `duration_cast` for every skeleton to service a 2 Hz
  timer. Move refresh off the hot path (periodic task / atomic `refreshDue` flag).
- **2.2 Replace `shared_mutex` + `unordered_map` + `shared_ptr` with a lock-free
  published snapshot** (immutable flat array, atomically swapped on refresh; hot
  path does an acquire-load + linear scan). Kills the SRWLock cache-line
  contention and the per-lookup refcount churn. For ≤~20 tracked actors a linear
  scan of a flat array beats a hash map for both hits and the dominant misses.
- **2.3** `[[unlikely]]` the tracked-path branch. **2.4** drop the singleton guard.
- Measure with: `registry/*` (watch single-thread latency *and* contention scaling).

### Tier 3 — the solver
- **3.1 Precompute `invMass` at bind (mass is constant); hoist `invDt` out of the
  per-bone loop** (it's per-frame, not per-bone). Removes redundant divides. Free.
- **3.2 `__m128` `StepBone`** — the solver is 3 independent axes (x,y,z) + a dead
  lane: do all three at once with FMA. Store `BoneState`/`BoneParams` as
  `NiPoint3A` (16B-aligned) for clean loads; the rotation rows are already
  16B-aligned. Add `__vectorcall` + `__restrict` on the rig pointer.
- Measure with: `solver/*` `ns/bone`.

### Tier 4 — scaling (matters once M2 populates the registry)
- Distance/`modelCulled` culling **and rate-reduction** (step distant actors at
  15–30 Hz with a dt accumulator); sleep springs whose parent is still.
- Optional SoA/AoSoA batch integrate (8 bones/AVX2 reg) decoupled from the stamp.
- Prefetch the two scattered lines per bone (`parentNode->world`, `local[rigIndex]`).
- PGO for branch/layout tuning.

## Recommended order
1. Tier 1 + 3.1 (minutes, zero risk).
2. Tier 2.1 + 2.2 (the scaling fix — do before M2).
3. Tier 3.2 (solver codegen ceiling).
4. Tier 4 as M2/M3 land.

## Validation
- **Offline, correctness:** `bench/fps_test.cpp` (the `check` target / `fps-test`)
  asserts the jiggle displacement curve matches across simulated 30/60/144 fps to
  within tolerance — the framerate-normalization gate. Run via `make check` or
  `xmake run fps-test`; CI gates on it.
- **Offline, relative:** `bench/` before/after every change (see its README).
- **In-game, absolute:** Superluminal / VTune attached to a running Starfield,
  sampling inside `Hook_ModelNodeUpdate` while walking a populated area. The
  offline harness can't model engine threading or real cache state — use it for
  the delta, the in-game profile for the truth.
