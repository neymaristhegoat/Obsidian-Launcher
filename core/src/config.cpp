#include "launcher/config.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace launcher::config {
namespace {

std::mutex g_mutex;
fs::path g_data_dir;
constexpr auto kDefaultConfig = R"({"schemaVersion":1,"settings":null})";

void save(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write launcher configuration");
        file << value.dump(2);
        if (!file) throw std::runtime_error("Cannot save launcher configuration");
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(path, error);
        error.clear();
        fs::rename(temporary, path, error);
        if (error) {
            fs::remove(temporary);
            throw std::runtime_error("Cannot update launcher configuration: " + error.message());
        }
    }
}

}  // namespace

void initialize(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_data_dir = fs::path(data_dir);
    for (const auto& relative : {"java", "meta", "libraries", "assets/indexes",
                                 "assets/objects", "versions", "natives", "instances", "logs"})
        fs::create_directories(g_data_dir / relative);

    const auto path = g_data_dir / "config.json";
    if (!fs::exists(path)) {
        std::ofstream file(path, std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot create launcher configuration");
        file << kDefaultConfig;
    }
}

json read() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ifstream file(g_data_dir / "config.json");
    if (!file) return json{{"schemaVersion", 1}, {"settings", nullptr}};
    auto value = json::parse(file, nullptr, false);
    if (!value.is_object()) return json{{"schemaVersion", 1}, {"settings", nullptr}};
    return value;
}

json write_settings(const json& settings) {
    if (!settings.is_object()) throw std::runtime_error("Launcher settings must be an object");
    std::lock_guard<std::mutex> lock(g_mutex);
    json value{{"schemaVersion", 1}, {"settings", settings}};
    save(g_data_dir / "config.json", value);
    return value;
}

}  // namespace launcher::config
