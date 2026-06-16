# OSF CBPC — Design & Roadmap

A per-bone spring/damper jiggle solver that drives Starfield's own rig bones at
runtime (CBPC-lite).

## Why this shape

Research (June 2026) pinned down four constraints:

1. The game doesn't spring Pecs/Belly/Butt. `BGSBoneModifier` is an inert editor
   record, so we can be the sole driver with nothing to fight.
2. Native jiggle is asset-baked, per-outfit, and collision-less. We run our own runtime solver (like HDT-SMP/CBPC) and only read/write bone transforms, we never touch the game's un-RE'd Havok.
3. The rig stamp is absolute, not additive, so we stamp partially: touch only
   jiggle-bone slots and add our offset on top of the pose already in the buffer.
   Jiggle bones are disjoint from animation bones, so this layers cleanly over the
   game's animation without the full-rig conflict.
4. Only `BGSModelNode::Update` (slot 2), pre-orig, survives. It composes and
   commits in the same call, and keeps firing for AI-frozen actors (unlike
   `AnimationManager::Update`). Integrate and write both happen there.

## Standalone, one hook

This is its own plugin. It installs its own `BGSModelNode::Update` slot-2 hook. The hot path
receives the live `BGSModelNode*`, so the registry is keyed by it. slot 2 fires once per
skeleton per frame, a per-actor wall-clock `dt` replaces the whole `FrameClock`/owner-token machinery that existed to tame the 7×/frame dual-manager
slot-4 path.

## Data flow

```
BGSModelNode::Update (slot 2, vtbl 400534 / fn 48634)        ← single write window
  └─ Hook_ModelNodeUpdate (PRE-orig)            PhysicsManager.cpp
       MaybeRefresh()                            ← throttled → game-thread RefreshActors()
       if enabledCount>0 and registry[a_this]:
         JiggleActor::Update(a_this)             ← bind, dt, StepBone per bone, additive stamp
       _origModelNodeUpdate(...)                 ← engine composes+commits our perturbed locals
```

## The solver (`JiggleSolver::StepBone`)

Per jiggle bone B with parent P, each frame:

1. Read P's world transform from `NiAVObject::world` (translate + rotate).
2. Finite-difference to world velocity, then world acceleration `aWorld` (1-frame
   latency, fine for jiggle). Huge jump ⇒ teleport guard, reset.
3. `aLocal = R_P^T · aWorld` - bring the drive into B's local frame so the offset
   rotates with the body.
4. Semi-implicit Euler per axis: `F = -k·x - c·v - mass·aLocal + gravity`;
   `v += F·dt/mass`; `x += v·dt`; clamp `|x|`.
5. Return `x`; the actor adds it to B's animated local translation.

v1 drives translation only, reads as jiggle, no transpose hazard. A rotation
variant is a later refinement.

## Roadmap

- **M0 (done):** scaffold - hook + verify, player-only registry, solver, JSON
  profiles, additive partial stamp.
- **M1:** spike-verify (constant offset → confirm visible + convention), confirm
  bone names and that vanilla leaves them static, tune.
- **M2:** `ProcessLists` high/middle-high enumeration → all nearby actors; cull by
  distance and `BSAnimationUpdateData::modelCulled`.
- **M3:** optional analytic self-collision spheres (NiPoint3 `GetSquaredDistance`,
  push displacement out of penetration). No Havok. Cross-actor collision later —
  needs a published-snapshot scheme, since slot 2 is multi-threaded per skeleton.
- **M4:** save-load `DropAll` sink; equip-rebind sink; promote profiles to OSF
  Body contract data; refactor to OSF Animation's post-stamp extension point.

## Open questions

1. Does `BGSModelNode::Update` compose `rig->local`/`world` for non-animated NPCs
   every frame? If not, M2 can't read their pose. We've only observed it for
   animated actors.
2. Real jiggle-bone names and parents - the profile uses unverified
   SF-Extended-Skeleton names. Bind is by lowercased name.
3. Does vanilla animate Pecs/Belly/Butt via any other subsystem (ragdoll, anim
   track)? If so, partial-additive fights it.
4. `NiMatrix3::entry[]` layout (used by `Math::TransposeMul`) — confirm against the
   CLSF fork if rotations look mirrored.