# OSF Body Physics — Design & Roadmap

CBPC-lite: a per-bone **spring/damper** jiggle solver that drives Starfield's own
rig bones at runtime. This doc is the architecture rationale; `../AGENTS.md` is
the live status brief and `RE.md` is the engine ground truth.

## Why this shape

Research (June 2026) established the constraints this design is built around:

1. **The game does not spring Pecs/Belly/Butt.** `BGSBoneModifier` is an inert
   editor record, so we can be the sole driver — no fighting the engine.
2. **Starfield's native jiggle is asset-baked, per-outfit, collision-less.** A
   runtime solver is the genuine gap; nobody has shipped one. We run our OWN
   solver (like HDT-SMP/CBPC) and only read/write bone transforms — we never call
   the game's (un-RE'd) Havok.
3. **The rig stamp is absolute, not additive.** So we are a *partial* stamper:
   touch only jiggle-bone slots, ADD our offset on top of the pose already in the
   buffer. Jiggle bones are disjoint from animation bones, so this layers over the
   game's animation (and OSF Animation) without the SAF/NAFSF full-rig conflict.
4. **Only `BGSModelNode::Update` (slot 2), pre-orig, survives.** That same call
   composes+commits, and it keeps firing for AI-frozen actors (unlike
   `AnimationManager::Update`). So both the integrate and the write happen there.

## Standalone, one hook

This is its own plugin. It installs its own `BGSModelNode::Update` slot-2 hook
(verify-before-patch, mirroring OSF Animation's `InstallHooks`). Because the hot
path receives the live `BGSModelNode*`, the registry is keyed by it and the hot
path needs **no loadedData chain** — we already hold the model node. And because
slot-2 fires exactly **once per skeleton per frame**, a simple per-actor
wall-clock `dt` replaces the whole `FrameClock`/owner-token machinery (that
existed to tame the 7×/frame, dual-manager slot-4 path).

### Coexistence with OSF Animation

Both hook slot 2. vtable hooks chain, so whoever installs LAST runs its pre-orig
work first. Since the two stampers write disjoint bone sets, they never clobber
each other. The only effect of order: for an actor OSF Animation is actively
scene-animating, if we run before its `StampPose` we read the *engine* parent
pose instead of the *scene* pose for the drive term — a fidelity nuance on scene
participants only; normal animated actors are always correct. The clean fix
(deferred) is for OSF Animation to expose a **post-stamp extension point** we
register into, so the core stays the only engine-hooking plugin.

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
1. Read P's world transform from its `NiAVObject::world` (translate + rotate).
2. Finite-difference → world velocity → world acceleration `aWorld` (1-frame
   latency; fine for jiggle). Huge jump ⇒ teleport guard, reset.
3. `aLocal = R_P^T · aWorld` — bring the drive into B's local frame so the offset
   rotates with the body.
4. Semi-implicit Euler per axis: `F = -k·x - c·v - mass·aLocal + gravity`;
   `v += F·dt/mass`; `x += v·dt`; clamp `|x|`.
5. Return `x`; the actor adds it to B's animated local translation.

v1 drives **translation only** (reads as jiggle, no transpose hazard). A rotation
variant (compose a small quat into the rows — reuse OSF Animation's
`MatrixToQuat`/`QuatToMatrix3x3`) is a later refinement.

## Roadmap

- **M0 (done):** scaffold — hook + verify, player-only registry, solver, JSON
  profiles, additive partial stamp.
- **M1:** spike-verify (constant offset → confirm visible + convention), confirm
  bone names + that vanilla leaves them static, tune.
- **M2:** `ProcessLists` high/middle-high enumeration → all nearby actors; cull by
  distance and `BSAnimationUpdateData::modelCulled`.
- **M3:** optional **analytic self-collision** spheres (NiPoint3 `GetSquaredDistance`,
  push displacement out of penetration). No Havok. Cross-actor collision later
  (needs a published-snapshot scheme — slot-2 is multi-threaded per skeleton).
- **M4:** save-load `DropAll` sink; equip-rebind sink; promote profiles to OSF
  Body contract data; refactor to OSF Animation's post-stamp extension point.

## Open questions (settle before M2 — use the OSF RE sandbox)

1. Does `BGSModelNode::Update` compose `rig->local`/`world` for **non-animated
   NPCs** every frame? If not, M2 can't read their pose. (We only ever observed it
   for animated actors.)
2. Real jiggle-bone names + parents (profile uses unverified SF-Extended-Skeleton
   names). Bind is by lowercased name.
3. Does vanilla animate Pecs/Belly/Butt via any other subsystem (ragdoll, anim
   track)? If so, partial-additive fights it.
4. `NiMatrix3::entry[]` layout (used by `Math::TransposeMul`) — confirm against the
   CLSF fork if rotations look mirrored.

## OSF Body bridge

A physics profile (bone set + parent links + per-bone tuning + future collider
spheres) is exactly the versioned, name-keyed, skeleton-relative contract OSF Body
already governs for morphs. The intent: OSF Body's Python tooling validates these
profiles (names against the frozen skeleton, seam-lock bones excluded, parents
resolvable) and ships them. The native solver consumes a contract the tooling
guarantees — unifying the two halves of the workspace.
