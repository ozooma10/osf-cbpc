#pragma once

// Per-actor jiggle state + the hot-path update. One JiggleActor per tracked
// actor; the registry (PhysicsManager) keys them by their live BGSModelNode*,
// which is exactly the pointer the slot-2 hook receives — so the hot path needs
// no loadedData chain (we already hold the model node). Update() runs PRE-orig
// inside Hook_ModelNodeUpdate, under `lock`, and does a PARTIAL ADDITIVE stamp:
// it touches only the configured jiggle-bone slots, adding the spring offset on
// top of whatever pose (engine animation or OSF Animation) is already in the
// rig buffer. Disjoint bone sets => no full-rig conflict (docs/DESIGN.md).

#include "Physics/JiggleSolver.h"
#include "Physics/PhysicsConfig.h"

namespace OSF::Physics
{
	class JiggleActor
	{
	public:
		std::mutex lock;

		JiggleActor(RE::TESObjectREFR* a_refr, std::shared_ptr<const Profile> a_profile);

		// refr -> loadedData(LockRead) -> data3D (BSFadeNode) -> BGSModelNode.
		// Returns the model node used as the registry key, or null if not loaded.
		static const RE::BGSModelNode* ResolveModelNode(RE::TESObjectREFR* a_refr);

		RE::TESObjectREFR* Target() const { return refr.get(); }

		// True while the actor's 3D is still resolvable (used to prune the registry).
		bool StillValid();

		// Hot path: bind-if-needed, compute dt, step each spring, additive-stamp.
		// Caller holds `lock` and guarantees a_modelNode is the live node for this
		// actor (the slot-2 hook's a_this).
		void Update(const RE::BGSModelNode* a_modelNode);

		// Reset every spring (call on save-load — world-anchored state is stale).
		void DropState();

	private:
		// (Re)resolve each profile bone + its parent to {rigIndex, parent NiAVObject*}
		// against the model node's bone map, by lowercased name.
		void Rebind(const RE::BGSModelNode* a_modelNode);

		// Per-actor wall-clock dt for the slot-2 (1x/skeleton/frame) path: clamped
		// to (0, 0.1] and zeroed on menu-pause / resume spikes. No FrameClock
		// needed here — slot-2 fires exactly once per skeleton per frame.
		float TickDt();

		RE::NiPointer<RE::TESObjectREFR> refr;
		std::shared_ptr<const Profile>   profile;

		struct BoundBone
		{
			std::uint16_t   boneRig{ 0 };           // write index into rig->local->data
			RE::NiAVObject* parentNode{ nullptr };  // motion source (read world transform)
			BoneParams      params;
			BoneState       state;
		};
		std::vector<BoundBone> bones;

		const RE::BGSModelNode* cachedModelNode{ nullptr };
		std::uint32_t           cachedBoneCount{ 0 };

		std::int64_t lastTickNs{ 0 };
		bool         loggedBind{ false };
		bool         loggedNodeDump{ false };  // one-shot skeleton bone-name dump (diagnostic)
	};
}
