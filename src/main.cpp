#include "Physics/PhysicsConfig.h"
#include "Physics/PhysicsManager.h"

namespace
{
	// Last game build whose engine offsets were manually verified. The single
	// vtable hook self-disables on a mismatch (see PhysicsManager::InstallHooks);
	// the layout-fact offsets it reads are NOT AddressLib-gated, so re-verify
	// docs/RE.md after any patch (symptom of drift: exploding / frozen jiggle
	// while the feature report still says INSTALLED).
	constexpr REL::Version kVerifiedGameVersion{ 1, 16, 244, 0 };

	void LogFeatureReport()
	{
		const bool hooks = OSF::Physics::PhysicsManager::GetSingleton().HooksInstalled();
		REX::INFO("Feature report: jiggle hook {} "
			"('unavailable' = the BGSModelNode::Update vtable verification refused this game build; "
			"see the matching error above)",
			hooks ? "INSTALLED" : "UNAVAILABLE");
	}

	void MessageCallback(SFSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SFSE::MessagingInterface::kPostLoad:
			// Every SFSE plugin's load handler has run, so OSF Animation / SAF /
			// NAF (if present) are in-process for the coexistence probe.
			OSF::Physics::PhysicsManager::GetSingleton().ProbeCoexistence();
			break;
		case SFSE::MessagingInterface::kPostDataLoad:
			OSF::Physics::Config::GetSingleton().LoadAll();
			OSF::Physics::PhysicsManager::GetSingleton().RegisterLoadEventSinks();
			LogFeatureReport();
			break;
		default:
			break;
		}
	}
}

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);
	return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	const auto runtime = a_sfse->RuntimeVersion();
	REX::INFO("{} v{} loading — supported game version {}, running on {}",
		SFSE::GetPluginName(), SFSE::GetPluginVersion().string(),
		kVerifiedGameVersion.string(), runtime.string());
	if (runtime != kVerifiedGameVersion) {
		REX::WARN("Unsupported game version: OSF Body Physics supports Starfield {} only, but this is {}. "
			"The jiggle hook self-disables on a mismatch — update the game to {} (or wait for a plugin update).",
			kVerifiedGameVersion.string(), runtime.string(), kVerifiedGameVersion.string());
	}

	OSF::Physics::PhysicsManager::GetSingleton().InstallHooks();
	SFSE::GetMessagingInterface()->RegisterListener(MessageCallback);

	REX::INFO("Load complete");
	return true;
}
