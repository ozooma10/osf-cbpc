#pragma once

// Physics profiles: which bones jiggle, their parent (motion source), and the
// per-bone spring tuning. Loaded from JSON under
//   Data/SFSE/Plugins/OSF CBPC/profiles/*.json
// at kPostDataLoad. Falls back to a built-in default if none are present.
//
// THE OSF BODY BRIDGE: a profile is exactly the kind of versioned, name-keyed
// contract OSF Body already governs for morphs. The intent is for OSF Body's
// Python tooling to validate these profiles (bone names against the frozen
// skeleton, seam-lock bones excluded, parent links resolvable) and ship them as
// part of the body contract. See docs/DESIGN.md "OSF Body bridge".

#include "Physics/JiggleSolver.h"

namespace OSF::Physics
{
	struct BoneSpec
	{
		std::string bone;     // skeleton node name to drive (e.g. "Pecs.L")
		std::string parent;   // node whose world motion drives it (e.g. "Spine3")
		BoneParams  params;
	};

	struct Profile
	{
		std::string           name;
		std::vector<BoneSpec> bones;
	};

	class Config
	{
	public:
		static Config& GetSingleton();

		// Scan the profiles directory and parse every *.json. Always leaves at
		// least the built-in "vanilla-female" default available.
		void LoadAll();

		// Returns the named profile, or the built-in default if unknown.
		std::shared_ptr<const Profile> GetProfile(std::string_view a_name) const;

	private:
		std::unordered_map<std::string, std::shared_ptr<const Profile>> profiles;
	};

	// Built-in fallback so M0 works with no JSON shipped. NOTE: the bone names
	// here are the SF Extended Skeleton physics bones reported by research and
	// MUST be verified against the actual skeleton (docs/RE.md open question).
	std::shared_ptr<const Profile> DefaultVanillaFemale();
}
