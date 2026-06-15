# OSF Body Physics

SFSE plugin — a **CBPC-lite** spring/damper body-jiggle solver for Starfield.
Standalone (no OSF Animation dependency); drives the engine's own rig bones at
runtime. Always-loaded session brief. Deeper docs: architecture & roadmap →
**docs/DESIGN.md** · engine offsets / post-patch surface → **docs/RE.md**.

- **Build:** C++23, **xmake only** (no CMake/vcpkg). GPL-3.0. CLSF submodule at
  `lib/commonlibsf` on branch `forge` (fork `ozooma10/commonlibsf`). Based on
  libxse/commonlibsf-template; build setup mirrors the sibling `OSF Animation`.
- **Version:** offsets verified against **1.16.244.0**. The one vtable hook
  self-disables on a slot mismatch; the layout-fact offsets it READS are not
  AddressLib-gated → re-verify **docs/RE.md** after any game patch.
- **repo == xmake target == MO2 mod == `OSF Body Physics`.** Deploy via
  `XSE_SF_MODS_PATH`.

## The one invariant (do not violate)

**Partial, additive, single-write-point, ordered-last.** The solver writes ONLY
its configured jiggle-bone slots in `rig->local->data`, ADDING the spring offset
on top of the pose already there, PRE-orig in `BGSModelNode::Update`. It must
never do a full-rig overwrite — that is the SAF/NAFSF last-writer-wins conflict.
Jiggle bones are disjoint from animation bones, which is *why* this coexists with
the game's animation and with OSF Animation.

## Architecture (src/)

- **main.cpp** — SFSE entry; version-gate WARN; `InstallHooks()`; profile load +
  feature report at kPostDataLoad.
- **Physics/PhysicsManager** — the singleton: installs the BGSModelNode::Update
  (slot 2) hook (verify-before-patch), owns the actor registry keyed by live
  `BGSModelNode*` + an atomic idle gate, throttles `RefreshActors` to the game
  thread. `Hook_ModelNodeUpdate` is the hot path.
- **Physics/JiggleActor** — per-actor state + the hot path: resolve/bind bones by
  name, per-actor wall-clock dt (slot-2 is 1×/skeleton/frame, so no FrameClock),
  step springs, additive-stamp. `ResolveModelNode` is the refr→model-node chain.
- **Physics/JiggleSolver** — pure spring/damper math (`StepBone`): parent world
  accel → bone-local frame → semi-implicit Euler + gravity + clamp.
- **Physics/PhysicsConfig** — JSON profile loader (bone/parent/tuning); built-in
  `vanilla-female` fallback. The **OSF Body contract bridge**.
- **Util/Math.h** — `TransposeMul` (world→local), `ClampAbs`.

## Status

- **[DONE] M0 scaffold:** single hook installs + verifies; player-only registry;
  spring solver; JSON profiles; additive partial stamp. Should produce visible
  jiggle on the player once bone names are confirmed.
- **[TODO] M1:** tune; confirm bone names + that vanilla leaves them static.
- **[TODO] M2:** ProcessLists actor population + distance/`modelCulled` culling.
- **[TODO] M3:** analytic self-collision spheres.
- **[TODO] M4:** save-load DropAll sink; equip-rebind sink; OSF Body profile
  contract + the core-hosted extension-point refactor.

## Gotchas

- **Do NOT transpose** when writing rotations (NiTransform rows are byte-identical
  to ozz column-major). v1 writes translation only.
- **rig->local->data writes only survive PRE-orig of BGSModelNode::Update.**
  Writing `node->local` does nothing for mapped bones; the slot-4
  (AnimationManager::Update) path is dead for rig writes.
- **dt must be clamped** (≤0.1s, 0 on menu-pause) or springs explode after a
  pause/load. `JiggleActor::TickDt` does this.
- **Submodule rule:** never pin CLSF to an unpushed commit; push the fork branch
  first, then bump the pointer.
- Verify open questions in **docs/RE.md** before trusting M2 (does the rig buffer
  compose for non-animated NPCs? real bone names? NiMatrix3 layout?).
