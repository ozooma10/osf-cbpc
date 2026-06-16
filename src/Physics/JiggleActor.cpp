#include "Physics/JiggleActor.h"

#include <algorithm>
#include <cctype>

namespace OSF::Physics
{
	namespace
	{
		std::string ToLower(std::string_view a_in)
		{
			std::string out{ a_in };
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}

	JiggleActor::JiggleActor(RE::TESObjectREFR* a_refr, std::shared_ptr<const Profile> a_profile) :
		refr(RE::NiPointer<RE::TESObjectREFR>(a_refr)),
		profile(std::move(a_profile))
	{}

	const RE::BGSModelNode* JiggleActor::ResolveModelNode(RE::TESObjectREFR* a_refr)
	{
		if (!a_refr) {
			return nullptr;
		}
		// Take the 3D root under the loadedData lock as a NiPointer: a save-load
		// can tear 3D down on another thread (same hazard OSF Animation guards).
		RE::NiPointer<RE::BSFadeNode> root;
		{
			auto loadedData = a_refr->loadedData.LockRead();
			if (*loadedData == nullptr) {
				return nullptr;
			}
			root.reset(static_cast<RE::BSFadeNode*>((*loadedData)->data3D.get()));
		}
		if (!root) {
			return nullptr;
		}
		return root->bgsModelNode.get();
	}

	bool JiggleActor::StillValid()
	{
		return refr && ResolveModelNode(refr.get()) != nullptr;
	}

	void JiggleActor::Rebind(const RE::BGSModelNode* a_modelNode)
	{
		bones.clear();
		cachedModelNode = a_modelNode;
		cachedBoneCount = a_modelNode->nodes.size();
		if (!profile) {
			return;
		}

		// lowercased node name -> {rigIndex, NiAVObject*} for the whole skeleton.
		std::unordered_map<std::string, std::pair<std::uint16_t, RE::NiAVObject*>> byName;
		byName.reserve(cachedBoneCount);
		for (std::uint32_t i = 0; i < a_modelNode->nodes.size(); ++i) {
			const auto& entry = a_modelNode->nodes[i];
			if (!entry.node) {
				continue;
			}
			const char* name = entry.node->name.c_str();
			if (!name) {
				continue;
			}
			byName.emplace(ToLower(name), std::make_pair(entry.rigIndex, entry.node));
		}

		// One-shot diagnostic: dump the real skeleton node names so profiles can be
		// authored against them. Logs a keyword-filtered candidate list first, then
		// every node. Remove once the vanilla-female profile names are confirmed.
		if (!loggedNodeDump) {
			loggedNodeDump = true;
			static constexpr std::string_view kHints[] = {
				"breast", "pec", "nipple", "belly", "stomach", "butt", "glute", "bottom",
				"cheek_b", "thigh", "chest", "spine", "pelvis", "clavicle", "neck"
			};
			std::string candidates;
			std::string all;
			for (std::uint32_t i = 0; i < a_modelNode->nodes.size(); ++i) {
				const auto& entry = a_modelNode->nodes[i];
				if (!entry.node || !entry.node->name.c_str()) {
					continue;
				}
				const std::string name = entry.node->name.c_str();
				all += name;
				all += ' ';
				const std::string lower = ToLower(name);
				for (const auto hint : kHints) {
					if (lower.find(hint) != std::string::npos) {
						candidates += name;
						candidates += ' ';
						break;
					}
				}
			}
			REX::INFO("OSF CBPC: skeleton has {} nodes. LIKELY jiggle/anchor bones >>> {}",
				a_modelNode->nodes.size(), candidates.empty() ? "(no keyword matches)" : candidates);
			REX::INFO("OSF CBPC: ALL node names >>> {}", all);
		}

		for (const auto& spec : profile->bones) {
			const auto bone = byName.find(ToLower(spec.bone));
			if (bone == byName.end()) {
				if (!loggedBind) {  // warn once per skeleton identity, not every rebind
					REX::WARN("OSF CBPC: bone '{}' not on this skeleton — skipping", spec.bone);
				}
				continue;
			}
			// Frame source = the bone's TRUE rig parent (NiAVObject::parent), not a
			// guessed config name. The displacement we add to local translate is
			// relative to this exact parent, so using the real one removes shear/bias.
			RE::NiAVObject* boneNode = bone->second.second;
			RE::NiAVObject* parentNode = boneNode ? boneNode->parent : nullptr;
			if (!parentNode) {
				if (!loggedBind) {
					REX::WARN("OSF CBPC: bone '{}' has no parent node — skipping", spec.bone);
				}
				continue;
			}
			bones.push_back(BoundBone{ bone->second.first, parentNode, spec.params, BoneState{} });
		}

		if (!loggedBind) {
			loggedBind = true;
			REX::INFO("OSF CBPC: bound {}/{} jiggle bones (profile '{}', {} skeleton nodes)",
				bones.size(), profile->bones.size(), profile->name, cachedBoneCount);
		}
	}

	float JiggleActor::TickDt()
	{
		using namespace std::chrono;
		const std::int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
		const std::int64_t prev = lastTickNs;
		lastTickNs = now;

		if (auto* main = RE::Main::GetSingleton(); main && main->isGameMenuPaused) {
			return 0.0f;  // frozen while paused; next tick reseeds dt from `now`
		}
		if (prev == 0) {
			return 0.0f;  // first tick: just seed lastTickNs
		}
		const float dt = static_cast<float>(now - prev) * 1e-9f;
		constexpr float kMaxStep = 0.1f;  // reject pause/resume/loading catch-up spikes
		return (dt > 0.0f && dt <= kMaxStep) ? dt : 0.0f;
	}

	void JiggleActor::Update(const RE::BGSModelNode* a_modelNode)
	{
		auto* rig = a_modelNode->rig;
		if (!rig || !rig->local || !rig->local->data) {
			return;
		}

		if (a_modelNode != cachedModelNode || a_modelNode->nodes.size() != cachedBoneCount) {
			Rebind(a_modelNode);
		}
		if (bones.empty()) {
			return;
		}

		const float dt = TickDt();
		if (dt <= 0.0f) {
			return;  // paused / first frame / spike — hold, don't integrate
		}

		// rig->local->data is the flat NiTransform buffer the renderer consumes.
		// We add our spring offset to the animated local translation — PRE-orig,
		// so the engine's compose+commit (in _origModelNodeUpdate) picks it up.
		RE::NiTransform* local = rig->local->data;
		for (auto& bone : bones) {
			if (!bone.parentNode) {
				continue;
			}
			// rest = the bone's animated local translation this frame; the solver
			// returns the lag offset to layer on top of it.
			const RE::NiPoint3 rest = local[bone.boneRig].translate;
			const RE::NiPoint3 disp = StepBone(bone.state, bone.params,
				bone.parentNode->world.translate, bone.parentNode->world.rotate, rest, dt);
			local[bone.boneRig].translate = rest + disp;
		}
	}

	void JiggleActor::DropState()
	{
		for (auto& bone : bones) {
			bone.state.Reset();
		}
		lastTickNs = 0;
	}
}
