#include "Physics/PhysicsManager.h"

#include "Physics/PhysicsConfig.h"

#include <REX/W32/KERNEL32.h>

namespace OSF::Physics
{
	namespace
	{
		// BGSModelNode::Update — slot 2, sig (modelNode, &fadeNode->local, NiUpdateData*).
		// PRE-orig is the rig-buffer write point (that same call composes + commits).
		// IDs are the SAME ones OSF Animation verified on 1.16.244 (docs/RE.md);
		// re-verify after any game patch via the slot check below.
		constexpr REL::ID ModelNodeVTableID(400534);
		constexpr REL::ID ModelNodeUpdateFnID(48634);
		constexpr std::size_t ModelNodeUpdateVFuncIdx = 2;

		// How often to (re)scan for trackable actors, from the hot path.
		constexpr std::int64_t kRefreshIntervalMs = 500;

		std::int64_t NowMs()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		}
	}

	PhysicsManager& PhysicsManager::GetSingleton()
	{
		static PhysicsManager singleton;
		return singleton;
	}

	void PhysicsManager::InstallHooks()
	{
		REL::Relocation<uintptr_t> vtbl{ ModelNodeVTableID };
		REL::Relocation<uintptr_t> expected{ ModelNodeUpdateFnID };

		const uintptr_t slotValue =
			*reinterpret_cast<uintptr_t*>(vtbl.address() + ModelNodeUpdateVFuncIdx * sizeof(uintptr_t));
		if (slotValue != expected.address()) {
			REX::ERROR("BGSModelNode vtable slot {} = {:X}, expected BGSModelNode::Update at {:X} — "
				"AddressLib IDs stale for this game version, NOT patching (jiggle disabled)",
				ModelNodeUpdateVFuncIdx, slotValue, expected.address());
			return;
		}

		_origModelNodeUpdate = reinterpret_cast<ModelNodeUpdateFn*>(
			vtbl.write_vfunc(ModelNodeUpdateVFuncIdx, &Hook_ModelNodeUpdate));
		REX::INFO("Installed BGSModelNode::Update (slot {}) vtable hook (verified)", ModelNodeUpdateVFuncIdx);
	}

	void PhysicsManager::ProbeCoexistence()
	{
		// Both we and OSF Animation hook BGSModelNode::Update. Jiggle bones are
		// disjoint from animation bones, so the two stampers don't clobber each
		// other — but for an OSF-scene-animated actor, whichever installed its
		// hook LAST runs its pre-orig work first, which decides whether jiggle
		// reads the scene pose or the engine pose for the parent. Best-effort for
		// v1; the clean fix is a core-hosted post-stamp extension point (DESIGN).
		if (REX::W32::GetModuleHandleW(L"OSF Animation.dll") ||
			REX::W32::GetModuleHandleW(L"OSF Animation Core.dll")) {
			REX::INFO("Coexistence: OSF Animation detected — jiggle on its scene participants is best-effort "
				"(stamp ordering depends on plugin load order). Normal animated actors are unaffected.");
		}
		if (REX::W32::GetModuleHandleW(L"StarfieldAnimationFramework.dll") ||
			REX::W32::GetModuleHandleW(L"NAF.dll")) {
			REX::WARN("Coexistence: SAF/NAF detected — these stamp the rig too; verify jiggle ordering in-game.");
		}
	}

	void PhysicsManager::RegisterLoadEventSinks()
	{
		// TODO[M4]: subscribe a SaveLoad / TESLoadGame sink and call DropAll() so
		// world-anchored spring state never drives rebuilt skeletons after a load.
		// Pattern: OSF Animation src/Serialization/SaveSafety.* + GraphManager::StopAll.
	}

	void PhysicsManager::MaybeRefresh()
	{
		const std::int64_t now = NowMs();
		std::int64_t prev = lastRefreshMs.load(std::memory_order_relaxed);
		if (now - prev < kRefreshIntervalMs) {
			return;
		}
		if (!lastRefreshMs.compare_exchange_strong(prev, now, std::memory_order_relaxed)) {
			return;  // another thread is taking this tick
		}
		// 3D / ProcessLists access belongs on the game thread.
		SFSE::GetTaskInterface()->AddTask([]() { GetSingleton().RefreshActors(); });
	}

	void PhysicsManager::RefreshActors()
	{
		// --- M0: the player only. Replace with a ProcessLists scan in M2. ---
		auto* player = RE::PlayerCharacter::GetSingleton();
		const RE::BGSModelNode* mn = JiggleActor::ResolveModelNode(player);

		std::unique_lock l{ registryLock };

		// Prune entries whose actor / 3D is gone (or whose model node was rebuilt).
		for (auto it = registry.begin(); it != registry.end();) {
			if (!it->second->StillValid()) {
				it = registry.erase(it);
			} else {
				++it;
			}
		}

		if (player && mn && !registry.contains(mn)) {
			// Drop any stale player entry under a previous model-node key first.
			std::erase_if(registry, [&](const auto& kv) { return kv.second->Target() == player; });
			registry.emplace(mn, std::make_shared<JiggleActor>(player, Config::GetSingleton().GetProfile("vanilla-female")));
		}

		enabledCount.store(registry.size(), std::memory_order_relaxed);
	}

	void PhysicsManager::DropAll()
	{
		std::unique_lock l{ registryLock };
		registry.clear();
		enabledCount.store(0, std::memory_order_relaxed);
	}

	std::uint64_t PhysicsManager::Hook_ModelNodeUpdate(RE::BGSModelNode* a_this, void* a_parentTransform, void* a_updateData)
	{
		auto& pm = GetSingleton();
		pm.MaybeRefresh();  // throttled; dispatches the scan to the game thread

		if (a_this && pm.enabledCount.load(std::memory_order_relaxed) > 0) {
			std::shared_ptr<JiggleActor> actor;
			{
				std::shared_lock l{ pm.registryLock };
				if (auto it = pm.registry.find(a_this); it != pm.registry.end()) {
					actor = it->second;
				}
			}
			if (actor) {
				std::scoped_lock al{ actor->lock };
				actor->Update(a_this);  // partial additive stamp, PRE-orig
			}
		}

		return _origModelNodeUpdate(a_this, a_parentTransform, a_updateData);
	}
}
