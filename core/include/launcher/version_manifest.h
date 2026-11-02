#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace launcher::version_manifest {

// Fetches the current Mojang manifest and returns the versions for the UI.
// A previously downloaded manifest is returned when the network is unavailable.
nlohmann::json fetch(const std::string& data_dir);

}  // namespace launcher::version_manifest
