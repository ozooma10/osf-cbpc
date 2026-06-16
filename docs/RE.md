# Engine RE ground truth (OSF CBPC)

Every address/offset this plugin depends on, and the post-patch surface. All of
it is **inherited from the sibling `OSF Animation` project's `docs/RE.md`**,
verified there against **Starfield 1.16.244.0** — this plugin does not introduce
new reverse engineering, it reuses the same rig-write path. Cross-check against
that file (the authoritative source) when bumping the game or CLSF.

## The one hook (AddressLib-gated)

| Hook | vtable REL::ID | impl REL::ID | vfunc slot |
|---|---|---|---|
| `BGSModelNode::Update` | `400534` | `48634` | `2` |

`PhysicsManager::InstallHooks` reads the slot and byte-compares it to the
AddressLib-resolved impl address **before** patching; on mismatch it logs and
refuses (jiggle self-disables, the rest of the game is untouched). This is the
ONLY thing protected by an AddressLib gate.

- Signature: `uint64_t(BGSModelNode* a_this, void* a_parentTransform /* &fadeNode->local, NiTransform */, void* a_updateData /* BSAnimationUpdateData */)`.
- PRE-orig is the verified rig-write point: the same call composes worlds and
  commits them right after. Writing here survives; the `AnimationManager::Update`
  (slot 4) path does NOT — the engine's snapshot applier (vfunc 7) rewrites rig
  locals after it. Writing `NiAVObject::local` for a mapped bone also does nothing.
- It keeps firing for AI-frozen actors (their `AnimationManager::Update` stops) —
  which is why both the integrate and the write live here.

## Layout facts (NOT AddressLib-gated — re-verify visually after a patch)

These are struct offsets, not addresses; a patch can move them silently. Symptom
of drift: contorted / exploding / frozen jiggle while the feature report still
says INSTALLED. Confirm against OSF Animation `docs/RE.md` (its `POST_PATCH_CHECKLIST`).

- **Resolve chain:** `refr -> loadedData.LockRead() -> data3D` (`BSFadeNode`) `-> bgsModelNode` (`+0x180`) `-> rig`.
- **`BGSModelNode::Rig`:** `local`/`world`/`prevWorld` buffers; each `Buffer::data`
  is a `NiTransform[]` indexed by `rigIndex` (stride `0x40`). We write
  `rig->local->data[rigIndex].translate`; we read the parent bone's world via its
  `NiAVObject::world`.
- **`BGSModelNode::nodes`** (`+0x18`): `BSTArray<NodeEntry>`, `NodeEntry{ u16 rigIndex; NiAVObject* node @+8 }` — the bone map we bind by lowercased `node->name`.
- **`NiTransform`** (`0x40`): rotation rows `+0x00/+0x10/+0x20`, translation `+0x30`,
  scale `+0x3C`. Rotations are row-vector convention = byte-identical to ozz
  column-major: **do NOT transpose** when writing (v1 writes translation only).
- **`NiAVObject`:** `world` NiTransform `+0x80` (read parent motion from here);
  `parent` `+0x38`; `collisionObject` `+0x110` (unused — collision is M3, analytic).
- **`Main::isGameMenuPaused`** — gate dt to 0 while paused.

## Open RE questions (block M2/M3 — verify in the OSF RE sandbox)

1. **Does `BGSModelNode::Update` compose `rig->local`/`rig->world` for actors with
   NO active animation graph, every frame?** Decisive for reading arbitrary nearby
   NPCs' poses. We have only ever observed the buffer for actors we (or OSF
   Animation) were already stamping. → probe an idle NPC's `rig->world->data`.
2. **Real jiggle-bone + parent names** on the actual skeleton (the profile uses
   SF-Extended-Skeleton names from research: `Pecs.L/R`, `C_Belly`, `Butt.L/R`).
   Bind is by lowercased name; mismatches are skipped with a warning.
3. **Does vanilla drive Pecs/Belly/Butt** via ragdoll / an anim-graph track? If so,
   the partial-additive stamp fights another writer.
4. **`NiMatrix3::entry[]` layout** assumed by `Util/Math.h::TransposeMul` (rows of
   `.x/.y/.z`). Confirm in the CLSF `forge` fork.
5. **`rig->prevWorld` / `rig->parents` semantics** — not used yet (we keep our own
   parent-position history), but they could replace the finite-difference if
   confirmed (prevWorld = immediately-prior frame?).

## Post-patch checklist (minimal)

1. Update `kVerifiedGameVersion` in `src/main.cpp`.
2. Run the game; read the log. `jiggle hook INSTALLED` ⇒ the slot still matches.
   `UNAVAILABLE` ⇒ re-derive `400534`/`48634` from the new AddressLib (cross-check
   OSF Animation, which hooks the same function).
3. If INSTALLED but jiggle is contorted/exploding/absent, a layout fact above
   moved — re-verify against OSF Animation `docs/RE.md`.
