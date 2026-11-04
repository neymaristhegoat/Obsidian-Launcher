#include "launcher/mod_loader.h"

#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace launcher::mod_loader {
namespace {

constexpr auto kUserAgent = "Obsidian Launcher/0.1.0";

void check_cancelled(const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (cancelled && cancelled->load()) throw std::runtime_error("Installation cancelled");
}

bool safe_version(const std::string& value) {
    return !value.empty() && value.size() <= 100 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
           });
}

nlohmann::json get_json(const std::string& url,
                        const std::shared_ptr<std::atomic_bool>& cancelled) {
    check_cancelled(cancelled);
    cpr::ProgressCallback callback([&cancelled](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t,
                                                cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
        return !(cancelled && cancelled->load());
    });
    const auto response = cpr::Get(cpr::Url{url},
                                   cpr::Header{{"Accept", "application/json"},
                                               {"User-Agent", kUserAgent}},
                                   cpr::Timeout{60000}, callback);
    check_cancelled(cancelled);
    if (response.error)
        throw std::runtime_error("Fabric metadata request failed: " + response.error.message);
    if (response.status_code != 200)
        throw std::runtime_error("Fabric metadata request failed (HTTP " +
                                 std::to_string(response.status_code) + ")");
    auto result = nlohmann::json::parse(response.text, nullptr, false);
    if (result.is_discarded()) throw std::runtime_error("Fabric returned invalid JSON metadata");
    return result;
}

void append_array(nlohmann::json& destination, const nlohmann::json& source,
                  const char* key) {
    if (!source.is_object() || !source.contains(key) || !source[key].is_array()) return;
    if (!destination.contains(key) || !destination[key].is_array())
        destination[key] = nlohmann::json::array();
    for (const auto& item : source[key]) destination[key].push_back(item);
}

}  // namespace

FabricProfile fetch_latest_fabric_profile(
    const std::string& minecraft_version,
    const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (!safe_version(minecraft_version))
        throw std::runtime_error("Invalid Minecraft version for Fabric installation");

    const auto versions = get_json("https://meta.fabricmc.net/v2/versions/loader/" + minecraft_version,
                                   cancelled);
    if (!versions.is_array()) throw std::runtime_error("Fabric returned an invalid loader version list");

    std::string selected_version;
    for (const auto& item : versions) {
        if (!item.is_object() || !item.contains("loader") || !item["loader"].is_object()) continue;
        const auto& loader = item["loader"];
        if (loader.value("stable", false) && loader.contains("version") && loader["version"].is_string()) {
            selected_version = loader["version"].get<std::string>();
            break; // Fabric Meta returns newest versions first.
        }
    }
    if (selected_version.empty())
        throw std::runtime_error("No stable Fabric Loader version is available for Minecraft " + minecraft_version);
    if (!safe_version(selected_version))
        throw std::runtime_error("Fabric returned an invalid loader version identifier");

    const auto profile = get_json("https://meta.fabricmc.net/v2/versions/loader/" +
                                  minecraft_version + "/" + selected_version + "/profile/json",
                                  cancelled);
    if (!profile.is_object() || !profile.contains("inheritsFrom") ||
        profile["inheritsFrom"] != minecraft_version ||
        !profile.contains("mainClass") || !profile["mainClass"].is_string() ||
        !profile.contains("libraries") || !profile["libraries"].is_array())
        throw std::runtime_error("Fabric returned an incomplete launcher profile");

    return {selected_version, profile};
}

nlohmann::json merge_fabric_profile(const nlohmann::json& vanilla,
                                    const nlohmann::json& fabric) {
    if (!vanilla.is_object() || !fabric.is_object())
        throw std::runtime_error("Cannot combine invalid Minecraft and Fabric metadata");
    auto merged = vanilla;

    if (!merged.contains("libraries") || !merged["libraries"].is_array())
        merged["libraries"] = nlohmann::json::array();
    for (const auto& library : fabric.at("libraries")) merged["libraries"].push_back(library);

    merged["mainClass"] = fabric.at("mainClass");
    if (fabric.contains("arguments") && fabric["arguments"].is_object()) {
        if (!merged.contains("arguments") || !merged["arguments"].is_object())
            merged["arguments"] = nlohmann::json::object();
        append_array(merged["arguments"], fabric["arguments"], "jvm");
        append_array(merged["arguments"], fabric["arguments"], "game");
    }
    // Fabric's profile intentionally inherits vanilla assets, downloads and version type.
    return merged;
}

}  // namespace launcher::mod_loader
