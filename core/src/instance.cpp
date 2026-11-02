#include "launcher/instance.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace launcher::instance {
namespace {

std::mutex g_content_mutex;

bool valid_identifier(const std::string& id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return std::isalnum(c) || c == '-' || c == '_';
           });
}

std::string folder_slug(const std::string& name) {
    std::string slug;
    bool pending_dash = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (pending_dash && !slug.empty()) slug.push_back('-');
            pending_dash = false;
            slug.push_back(static_cast<char>(std::tolower(c)));
        } else {
            pending_dash = true;
        }
        if (slug.size() >= 48) break;
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "instance" : slug;
}

bool has_instance_id(const fs::path& path, const std::string& id) {
    std::ifstream file(path / "instance.json");
    if (!file) return false;
    const json value = json::parse(file, nullptr, false);
    return value.is_object() && value.value("id", std::string()) == id;
}

fs::path find_path(const fs::path& data_dir, const std::string& id) {
    const auto instances = data_dir / "instances";
    std::error_code error;
    if (fs::exists(instances, error)) {
        for (fs::directory_iterator it(instances, error), end; !error && it != end; it.increment(error)) {
            if (it->is_directory(error) && has_instance_id(it->path(), id)) return it->path();
        }
    }
    // Older builds placed profile files directly under the profile UUID.
    const auto legacy = instances / id;
    if (fs::exists(legacy)) return legacy;
    return {};
}

void write_json(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write instance configuration");
        file << value.dump(2);
        if (!file) throw std::runtime_error("Cannot save instance configuration");
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(path, error);
        error.clear();
        fs::rename(temporary, path, error);
        if (error) {
            fs::remove(temporary);
            throw std::runtime_error("Cannot update instance configuration: " + error.message());
        }
    }
}

void move_legacy_content(const fs::path& root) {
    const auto minecraft = root / ".minecraft";
    fs::create_directories(minecraft);
    const auto old_modrinth_index = root / "modrinth-installed.json";
    const auto new_modrinth_index = minecraft / "modrinth-installed.json";
    std::error_code metadata_error;
    if (fs::exists(old_modrinth_index, metadata_error) && !metadata_error &&
        !fs::exists(new_modrinth_index)) {
        fs::rename(old_modrinth_index, new_modrinth_index, metadata_error);
        if (metadata_error)
            throw std::runtime_error("Cannot migrate Modrinth install metadata: " + metadata_error.message());
    }
    for (const auto* folder : {"mods", "saves", "resourcepacks", "shaderpacks", "config", "logs"}) {
        const auto old_path = root / folder;
        const auto new_path = minecraft / folder;
        fs::create_directories(new_path);
        std::error_code error;
        if (!fs::exists(old_path, error) || error) continue;
        for (fs::directory_iterator it(old_path, error), end; !error && it != end; it.increment(error)) {
            auto destination = new_path / it->path().filename();
            if (fs::exists(destination)) {
                destination = new_path / (it->path().stem().string() + "-migrated" +
                                          it->path().extension().string());
            }
            fs::rename(it->path(), destination, error);
            if (error) throw std::runtime_error("Cannot migrate instance content: " + error.message());
        }
        fs::remove(old_path, error);
    }
}

}  // namespace

std::mutex& content_mutex() { return g_content_mutex; }

fs::path path_for(const std::string& data_dir, const std::string& instance_id) {
    if (!valid_identifier(instance_id)) throw std::runtime_error("Invalid instance identifier");
    const auto found = find_path(fs::path(data_dir), instance_id);
    return found.empty() ? fs::path(data_dir) / "instances" / instance_id : found;
}

json list(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(content_mutex());
    json result = json::array();
    const auto root = fs::path(data_dir) / "instances";
    std::error_code error;
    if (!fs::exists(root, error)) return result;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_directory(error)) continue;
        std::ifstream file(it->path() / "instance.json");
        if (!file) continue;
        const json profile = json::parse(file, nullptr, false);
        if (profile.is_object() && profile.contains("id") && profile.contains("name") &&
            profile.contains("version")) result.push_back(profile);
    }
    if (error) throw std::runtime_error("Cannot list instances: " + error.message());
    return result;
}

json save(const std::string& data_dir, const json& profile) {
    if (!profile.is_object()) throw std::runtime_error("Instance profile must be an object");
    const auto id = profile.value("id", std::string());
    const auto name = profile.value("name", std::string());
    const auto version = profile.value("version", std::string());
    if (!valid_identifier(id)) throw std::runtime_error("Invalid instance identifier");
    if (name.empty() || name.size() > 64) throw std::runtime_error("Instance name must be 1 to 64 characters");
    if (version.empty() || version.size() > 64) throw std::runtime_error("Invalid Minecraft version");

    std::lock_guard<std::mutex> lock(content_mutex());
    const fs::path root = fs::path(data_dir) / "instances";
    fs::create_directories(root);
    auto old_path = find_path(fs::path(data_dir), id);
    const auto suffix = id.substr(0, std::min<std::size_t>(8, id.size()));
    auto destination = root / (folder_slug(name) + "-" + suffix);
    if (fs::exists(destination) && !has_instance_id(destination, id))
        destination = root / (folder_slug(name) + "-" + id);

    if (!old_path.empty() && old_path != destination) {
        std::error_code error;
        fs::rename(old_path, destination, error);
        if (error) throw std::runtime_error("Cannot rename instance folder: " + error.message());
    }
    fs::create_directories(destination);
    move_legacy_content(destination);
    write_json(destination / "instance.json", profile);
    return profile;
}

void remove_files(const std::string& data_dir, const std::string& instance_id) {
    if (!valid_identifier(instance_id)) throw std::runtime_error("Invalid instance identifier");
    std::lock_guard<std::mutex> lock(content_mutex());
    const auto root = path_for(data_dir, instance_id);
    std::error_code error;
    fs::remove_all(root, error);
    if (error) throw std::runtime_error("Could not remove instance files: " + error.message());
}

}  // namespace launcher::instance
