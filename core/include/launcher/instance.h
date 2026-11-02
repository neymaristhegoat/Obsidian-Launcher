#pragma once

#include <string>
#include <mutex>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace launcher::instance {

void remove_files(const std::string& data_dir, const std::string& instance_id);
std::filesystem::path path_for(const std::string& data_dir, const std::string& instance_id);
nlohmann::json list(const std::string& data_dir);
nlohmann::json save(const std::string& data_dir, const nlohmann::json& profile);
std::mutex& content_mutex();

}  // namespace launcher::instance
