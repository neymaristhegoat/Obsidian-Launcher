#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace launcher::config {

void initialize(const std::string& data_dir);
nlohmann::json read();
nlohmann::json write_settings(const nlohmann::json& settings);

}  // namespace launcher::config
