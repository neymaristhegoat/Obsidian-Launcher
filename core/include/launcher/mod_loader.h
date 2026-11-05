#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace launcher::mod_loader {

struct FabricProfile {
    std::string loader_version;
    nlohmann::json profile;
};

// Latest stable Forge build published for the requested Minecraft version.
std::string fetch_latest_forge_version(
    const std::string& minecraft_version,
    const std::shared_ptr<std::atomic_bool>& cancelled);

// Fetches the newest stable Fabric Loader compatible with the requested game version.
FabricProfile fetch_latest_fabric_profile(
    const std::string& minecraft_version,
    const std::shared_ptr<std::atomic_bool>& cancelled);

// Combines Fabric's launcher profile with the Mojang version metadata.
nlohmann::json merge_fabric_profile(const nlohmann::json& vanilla,
                                    const nlohmann::json& fabric);

// Combines the Forge version manifest with its inherited Mojang version.
nlohmann::json merge_forge_profile(const nlohmann::json& vanilla,
                                   const nlohmann::json& forge);

}  // namespace launcher::mod_loader
