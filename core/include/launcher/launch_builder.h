#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace launcher::launch_builder {

nlohmann::json build(const std::string& data_dir, const nlohmann::json& params);

}  // namespace launcher::launch_builder
