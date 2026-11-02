#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace launcher::minecraft_installer {

using Progress = std::function<void(const std::string& message, int percent, const std::string& state)>;

nlohmann::json install(const std::string& data_dir,
                       const std::string& instance_id,
                       const std::shared_ptr<std::atomic_bool>& cancelled,
                       const Progress& progress);

}  // namespace launcher::minecraft_installer
