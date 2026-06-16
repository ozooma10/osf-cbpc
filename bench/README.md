# OSF Body Physics — performance harness

A self-contained microbenchmark for tracking how changes affect the plugin's two
hot-path cost regimes (see `../docs/PERF.md`). Run it **before** a change, run it
**after**, diff the numbers. It compiles the real solver and reproduces the real
CLSF vector-operator codegen, so build flags (LTO / AVX2 / fp-contract) and the
SIMD rewrite all show up in the numbers.

## What it measures

| Benchmark | Cost regime | Models | Sensitive to |
|---|---|---|---|
| `solver/*` | **B** — per-tracked-bone work | the exact inner loop of `JiggleActor::Update` calling the real `JiggleSolver::StepBone` over the real out-of-line CLSF `NiPoint3`/`NiMatrix3` operators | LTO, `/arch:AVX2`, `/fp:contract`, the `__m128` solver rewrite (Tier 1 + Tier 3) |
| `registry/*` | **A** — the per-skeleton hook tax | the `BGSModelNode::Update` hook lookup that fires for *every* skeleton, every frame, on multiple threads: current `shared_mutex` + `unordered_map` + clock read vs. the proposed lock-free published snapshot | the registry rework (Tier 2) |

`registry/*` is reported single-threaded (clean per-lookup latency) and
multi-threaded (the SRWLock cache-line contention that grows with crowd size ×
core count). The headline line estimates the per-frame hook tax for a crowd.

## Build & run

**Portable (verified, Linux/CI) — Makefile:**
```sh
cd bench
make CONFIG=release && ./build/bench-release      # baseline: -O2, no LTO
make CONFIG=lto      && ./build/bench-lto          # Tier 1.1: -flto
make CONFIG=avx2     && ./build/bench-avx2         # Tier 1.1+1.2+1.3
make CONFIG=fast     && ./build/bench-fast         # -O3 -flto -march=native
CXX=clang++ make CONFIG=avx2                        # compare compilers
make CONFIG=debug    && ./build/bench-debug        # ASan + UBSan correctness
```

**Game-representative (MSVC) — xmake:** the plugin ships as an MSVC DLL, so for
numbers closest to in-game codegen build with the same toolchain:
```sh
cd bench
xmake f -m release -y
xmake build bench  &&  xmake run bench              # baseline
xmake build bench-opt && xmake run bench-opt        # LTO + /arch:AVX2 + /fp:contract
```

### Useful flags
```
--bones N         springs in the player solver run (default 5 = vanilla-female)
--crowd-bones N   springs in the M2-scale run      (default 400 ≈ 80 actors × 5)
--no-scatter      sequential rig indices (best-case cache) instead of scattered
--skeletons N     scene size for the registry tax  (default 128)
--tracked N       tracked actors                   (default 1 = player only)
--threads N       worker threads for contention    (default = hardware threads)
--fps F           frame budget for the headline    (default 60)
--json --label X  machine-readable output tagged X (for recording baselines)
```

## Tracking changes over time

Local before/after, by hand (these `results/*.json` are gitignored scratch):
```sh
./build/bench-release --json --label before > results/before.json
# … make a change, rebuild …
./build/bench-release --json --label after  > results/after.json
python3 compare.py --baseline results/before.json --current results/after.json
```

## Continuous integration

`.github/workflows/bench.yml` runs this harness on every PR that touches the
solver, the harness, or the CLSF headers, and compares against the committed
baseline `results/ci-baseline.json` via `compare.py`. The delta table is posted
to the job summary; a *stable* metric regressing past 25% fails the check
(the noisy multi-thread contention numbers are reported but never gate).

The baseline must come from CI hardware to be comparable, so:
- `results/ci-baseline.json` is **regenerated and committed automatically on
  every push to `master`** (and via the *Update baseline* `workflow_dispatch`).
- The seed committed here is flagged `"placeholder": true` and is **advisory
  only** until that first CI refresh replaces it — so it never causes a spurious
  failure from cross-hardware differences.

CI pins `--threads 4` and a fixed workload (see `BENCH_ARGS` in the workflow) so
runs are comparable across runners.

## How to read it

- **`solver` `ns/bone`** — lower is better. Tier-1 flags trim it; the Tier-3
  `__m128` rewrite should cut it substantially. With ~5 bones on the player this
  is sub-microsecond per frame; it only matters at M2 crowd scale.
- **`registry current` vs `snapshot`** — the snapshot path is the proposed
  design. Watch two things: the single-thread latency gap, and the **contention
  scaling** line (`current` gets *worse* with more threads; `snapshot` stays
  flat). This is the dominant cost once a scene is populated.
- **`regime-A hook tax`** — estimated µs/frame the hook spends for `--skeletons`
  NPCs. This is the number that decides whether the plugin is shippable in a
  crowd.

## Caveats (important)

- This is an **offline** harness. It measures the solver math and the registry
  *data-structure patterns* faithfully, but **not** the real in-game environment
  (engine thread scheduling, real cache residency of the rig buffers, the actual
  `BGSModelNode::Update` call). Treat the **relative before/after delta** as the
  signal; the absolute ns will differ in-game. For true in-game validation,
  attach **Superluminal** or **Intel VTune** to a running Starfield and sample
  inside `Hook_ModelNodeUpdate` while walking a populated area.
- `clsf_ops.cpp` reproduces the CLSF operators **verbatim** (the real `.cpp`
  can't be compiled off-target — it binds game addresses via `REL::Relocation`
  and uses MSVC-only `std::sqrtf`). Same arithmetic, same separate-TU out-of-line
  property, so the LTO/inlining delta is real. If CLSF's operator bodies ever
  change, re-sync this file.
- The snapshot registry leaks retired snapshots *by design* during a run
  (deferred reclamation); the real plugin needs epoch/RCU/`atomic<shared_ptr>`
  reclamation. The **read-path** cost measured here is what that design targets.
