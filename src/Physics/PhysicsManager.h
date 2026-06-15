#pragma once

// The plugin's single engine hook + the tracked-actor registry.
//
// ONE vtable hook: BGSModelNode::Update (slot 2) — the same RE-proven per-frame
// rig-write point OSF Animation uses, but installed independently. Jiggle bones
// are disjoint from animation bones, so a PARTIAL pre-orig stamp coexists with
// the game's animation (and with OSF Animation) without the full-rig last-writer
// conflict that CompatWarning blocks for SAF/NAFSF. See docs/DESIGN.md.

#include "Physics/JiggleActor.h"

namespace OSF::Physics
{
	class PhysicsManager
	{
	public:
		static PhysicsManager& GetSingleton();

		// Verifies the BGSModelNode::Update vtable slot points at the expected
		// function before patching (guards against silently re-bound AddressLib
		// IDs); self-disables on mismatch. Call in SFSE_PLUGIN_LOAD.
		void InstallHooks();
		bool HooksInstalled() const { return _origModelNodeUpdate != nullptr; }

		// Detect OSF Animation / SAF / NAF in-process and log the coexistence
		// note (ordering of the two slot-2 stampers for scene-animated actors).
		void ProbeCoexistence();

		// Drop all spring state on a world-replacing load. TODO[M4]: wire to a
		// SaveLoad/TESLoadGame sink (see OSF Animation SaveSafety for the pattern).
		void RegisterLoadEventSinks();

		// (Re)populate the registry on the game thread. M0: the player only.
		// TODO[M2]: enumerate ProcessLists high/middle-high actors and filter
		// eligible humanoids; cull by distance / modelCulled.
		void RefreshActors();

		// Clear the registry (e.g. on load).
		void DropAll();

	private:
		static std::uint64_t Hook_ModelNodeUpdate(RE::BGSModelNode* a_this, void* a_parentTransform, void* a_updateData);
		using ModelNodeUpdateFn = std::uint64_t(RE::BGSModelNode*, void*, void*);
		static inline ModelNodeUpdateFn* _origModelNodeUpdate = nullptr;

		// Throttled dispatch of RefreshActors to the game thread (called from the
		// hot path; the registry must be populated even before any actor exists).
		void MaybeRefresh();

		std::shared_mutex registryLock;
		std::unordered_map<const RE::BGSModelNode*, std::shared_ptr<JiggleActor>> registry;

		// Lock-free idle gate: the hook fires for EVERY skeleton in the game every
		// frame forever, so with nothing tracked it must early-out without locking.
		std::atomic<std::size_t> enabledCount{ 0 };
		std::atomic<std::int64_t> lastRefreshMs{ 0 };
	};
}
