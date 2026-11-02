#include "launcher/version_manifest.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace launcher::version_manifest {

namespace {

constexpr auto kManifestUrl =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
std::mutex g_manifest_mutex;

fs::path cache_path(const std::string& data_dir) {
    return fs::path(data_dir) / "meta" / "version_manifest.json";
}

json read_cached_manifest(const fs::path& path) {
    std::ifstream file(path);
    if (!file) return nullptr;

    const json cached = json::parse(file, nullptr, false);
    if (!cached.is_object()) return nullptr;

    const json manifest = cached.value("manifest", json::object());
    if (!manifest.is_object() || !manifest.contains("versions") ||
        !manifest["versions"].is_array()) {
        return nullptr;
    }
    return cached;
}

json to_response(const json& cached, const std::string& source) {
    const auto& manifest = cached.at("manifest");
    json versions = json::array();
    for (const auto& item : manifest.at("versions")) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string())
            continue;

        versions.push_back({
            {"id", item["id"]},
            {"type", item.value("type", std::string("release"))},
            {"url", item.value("url", std::string())},
            {"time", item.value("time", std::string())},
            {"releaseTime", item.value("releaseTime", std::string())},
            {"sha1", item.value("sha1", std::string())},
            {"complianceLevel", item.value("complianceLevel", 0)},
        });
    }
    if (versions.empty()) throw std::runtime_error("Minecraft version manifest contains no versions");

    return {{"versions", std::move(versions)},
            {"source", source},
            {"fetchedAt", cached.value("fetchedAt", std::int64_t{0})}};
}

void save_cached_manifest(const fs::path& path, const json& manifest) {
    fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write Minecraft manifest cache");
        file << json{{"fetchedAt", std::chrono::system_clock::to_time_t(
                                      std::chrono::system_clock::now())},
                     {"manifest", manifest}}
                    .dump();
        if (!file) throw std::runtime_error("Cannot save Minecraft manifest cache");
    }

    std::error_code ec;
    fs::rename(temporary, path, ec);
    if (ec) {
        // Windows does not replace an existing destination with rename().
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
        if (ec) {
            fs::remove(temporary);
            throw std::runtime_error("Cannot update Minecraft manifest cache: " + ec.message());
        }
    }
}

}  // namespace

json fetch(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(g_manifest_mutex);
    const auto path = cache_path(data_dir);

    auto response = cpr::Get(
        cpr::Url{kManifestUrl},
        cpr::Header{{"Accept", "application/json"}, {"User-Agent", "Obsidian Launcher/0.1.0"}},
        cpr::Timeout{15000});

    if (!response.error && response.status_code == 200) {
        const auto manifest = json::parse(response.text, nullptr, false);
        if (!manifest.is_discarded() && manifest.is_object() &&
            manifest.contains("versions") && manifest["versions"].is_array()) {
            const json current{{"fetchedAt", std::chrono::system_clock::to_time_t(
                                                   std::chrono::system_clock::now())},
                               {"manifest", manifest}};
            try {
                save_cached_manifest(path, manifest);
                const auto cached = read_cached_manifest(path);
                if (cached.is_object()) return to_response(cached, "network");
            } catch (const std::exception& error) {
                spdlog::warn("could not cache Minecraft version manifest: {}", error.what());
            }
            return to_response(current, "network");
        }
        spdlog::warn("Mojang returned an invalid Minecraft version manifest");
    }

    const std::string failure = response.error
        ? "Network error: " + response.error.message
        : "Mojang version manifest request failed (HTTP " +
              std::to_string(response.status_code) + ")";
    const auto cached = read_cached_manifest(path);
    if (cached.is_object()) {
        spdlog::warn("{}; using cached Minecraft version manifest", failure);
        return to_response(cached, "cache");
    }

    throw std::runtime_error(failure + ". No cached manifest is available.");
}

}  // namespace launcher::version_manifest
