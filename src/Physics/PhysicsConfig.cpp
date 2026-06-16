#include "Physics/PhysicsConfig.h"

namespace OSF::Physics
{
	namespace
	{
		constexpr const char* kProfileDir = "Data/SFSE/Plugins/OSF CBPC/profiles";
		constexpr const char* kDefaultName = "vanilla-female";

		// Read a 3-vector ([x,y,z]) from JSON, leaving the default if absent/malformed.
		void ReadVec3(const nlohmann::json& a_obj, const char* a_key, RE::NiPoint3& a_out)
		{
			if (auto it = a_obj.find(a_key); it != a_obj.end() && it->is_array() && it->size() == 3) {
				a_out.x = it->at(0).get<float>();
				a_out.y = it->at(1).get<float>();
				a_out.z = it->at(2).get<float>();
			}
		}

		BoneParams ParseParams(const nlohmann::json& a_bone)
		{
			BoneParams p;  // defaults from JiggleSolver.h
			ReadVec3(a_bone, "stiffness", p.stiffness);
			ReadVec3(a_bone, "damping", p.damping);
			ReadVec3(a_bone, "gravity", p.gravity);
			ReadVec3(a_bone, "maxOffset", p.maxOffset);
			if (auto it = a_bone.find("mass"); it != a_bone.end() && it->is_number()) {
				p.mass = it->get<float>();
			}
			return p;
		}

		std::shared_ptr<const Profile> ParseProfile(const nlohmann::json& a_json, const std::string& a_fallbackName)
		{
			auto profile = std::make_shared<Profile>();
			profile->name = a_json.value("profile", a_fallbackName);
			if (auto it = a_json.find("bones"); it != a_json.end() && it->is_array()) {
				for (const auto& bone : *it) {
					if (!bone.contains("bone") || !bone.contains("parent")) {
						continue;
					}
					profile->bones.push_back(BoneSpec{
						bone.at("bone").get<std::string>(),
						bone.at("parent").get<std::string>(),
						ParseParams(bone) });
				}
			}
			return profile;
		}
	}

	std::shared_ptr<const Profile> DefaultVanillaFemale()
	{
		auto p = std::make_shared<Profile>();
		p->name = kDefaultName;
		// Female jiggle bones. Vanilla has only L_Butt/R_Butt; L_Pecs/R_Pecs/C_Belly
		// bind only with an extended skeleton (skipped + warned otherwise). Names
		// follow the SF-Extended-Skeleton convention. Mirrors the shipped
		// dist/.../profiles/vanilla-female.json, which overrides this fallback.
		const auto mk = [](const char* a_bone, float a_k, float a_c, float a_max) {
			BoneParams bp;
			bp.stiffness = { a_k, a_k, a_k };
			bp.damping = { a_c, a_c, a_c };
			bp.gravity = { 0.0f, 0.0f, 0.0f };
			bp.maxOffset = { a_max, a_max, a_max };
			bp.mass = 1.0f;
			return BoneSpec{ a_bone, "(auto)", bp };
		};
		p->bones.push_back(mk("L_Butt", 450.0f, 28.0f, 0.6f));
		p->bones.push_back(mk("R_Butt", 450.0f, 28.0f, 0.6f));
		p->bones.push_back(mk("L_Pecs", 120.0f, 12.0f, 2.0f));
		p->bones.push_back(mk("R_Pecs", 120.0f, 12.0f, 2.0f));
		p->bones.push_back(mk("C_Belly", 500.0f, 28.0f, 0.4f));
		return p;
	}

	Config& Config::GetSingleton()
	{
		static Config singleton;
		return singleton;
	}

	void Config::LoadAll()
	{
		profiles.clear();
		profiles.emplace(kDefaultName, DefaultVanillaFemale());

		std::error_code ec;
		const std::filesystem::path dir{ kProfileDir };
		if (std::filesystem::exists(dir, ec)) {
			for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
				if (entry.path().extension() != ".json") {
					continue;
				}
				try {
					std::ifstream f{ entry.path() };
					const auto json = nlohmann::json::parse(f);
					auto profile = ParseProfile(json, entry.path().stem().string());
					REX::INFO("Config: loaded profile '{}' ({} bones) from {}",
						profile->name, profile->bones.size(), entry.path().filename().string());
					profiles[profile->name] = std::move(profile);
				} catch (const std::exception& e) {
					REX::ERROR("Config: failed to parse {} — {}", entry.path().filename().string(), e.what());
				}
			}
		}
		REX::INFO("Config: {} profile(s) available (default '{}')", profiles.size(), kDefaultName);
	}

	std::shared_ptr<const Profile> Config::GetProfile(std::string_view a_name) const
	{
		if (auto it = profiles.find(std::string{ a_name }); it != profiles.end()) {
			return it->second;
		}
		if (auto it = profiles.find(kDefaultName); it != profiles.end()) {
			return it->second;
		}
		return DefaultVanillaFemale();
	}
}
