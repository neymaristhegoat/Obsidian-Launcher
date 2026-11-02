#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace launcher::modrinth {

nlohmann::json search(const nlohmann::json& params);
nlohmann::json installed(const std::string& data_dir, const nlohmann::json& params);
nlohmann::json install(const std::string& data_dir, const nlohmann::json& params);
nlohmann::json remove(const std::string& data_dir, const nlohmann::json& params);

}  // namespace launcher::modrinth
