#include "launcher/mod_loader.h"

#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <regex>
#include <vector>
#include <unordered_set>
#include <cstdint>

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

std::string get_text(const std::string& url,
                     const std::shared_ptr<std::atomic_bool>& cancelled,
                     const std::string& source_name) {
    check_cancelled(cancelled);
    cpr::ProgressCallback callback([&cancelled](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t,
                                                cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
        return !(cancelled && cancelled->load());
    });
    const auto response = cpr::Get(cpr::Url{url},
                                   cpr::Header{{"User-Agent", kUserAgent}},
                                   cpr::Timeout{60000}, callback);
    check_cancelled(cancelled);
    if (response.error)
        throw std::runtime_error(source_name + " request failed: " + response.error.message);
    if (response.status_code != 200)
        throw std::runtime_error(source_name + " request failed (HTTP " +
                                 std::to_string(response.status_code) + ")");
    return response.text;
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


std::vector<std::uint64_t> numeric_version_parts(const std::string& value) {
    std::vector<std::uint64_t> parts;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto end = value.find('.', offset);
        const auto token = value.substr(offset, end == std::string::npos ? end : end - offset);
        if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return std::isdigit(ch);
            })) return {};
        try { parts.push_back(std::stoull(token)); }
        catch (...) { return {}; }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return parts;
}

bool newer_numeric_version(const std::string& left, const std::string& right) {
    const auto a = numeric_version_parts(left);
    const auto b = numeric_version_parts(right);
    if (a.empty()) return false;
    if (b.empty()) return true;
    const auto count = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto av = i < a.size() ? a[i] : 0;
        const auto bv = i < b.size() ? b[i] : 0;
        if (av != bv) return av > bv;
    }
    return false;
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


std::string fetch_latest_forge_version(
    const std::string& minecraft_version,
    const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (!safe_version(minecraft_version))
        throw std::runtime_error("Invalid Minecraft version for Forge installation");

    // Forge's official Maven metadata is the source of truth for every published build.
    // Keep the exact version suffix from Maven: older releases use extra suffixes
    // such as -mc172 or -1.7.10, and those suffixes are part of the artifact URL.
    const auto xml = get_text("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml",
                              cancelled, "Forge version metadata");
    const std::string prefix = minecraft_version + "-";
    const std::regex version_tag(R"(<version>\s*([^<]+?)\s*</version>)",
                                 std::regex_constants::icase);
    std::string latest_artifact_suffix;
    std::string latest_numeric_build;
    const auto minecraft_parts = numeric_version_parts(minecraft_version);

    for (std::sregex_iterator it(xml.begin(), xml.end(), version_tag), end; it != end; ++it) {
        const auto artifact_version = (*it)[1].str();
        if (!artifact_version.starts_with(prefix)) continue;
        const auto candidate = artifact_version.substr(prefix.size());
        const auto separator = candidate.find('-');
        const auto numeric_build = candidate.substr(0, separator);
        const auto build_parts = numeric_version_parts(numeric_build);
        if (build_parts.empty()) continue;

        // Permit the historical suffix formats documented by Forge's Maven
        // releases, but do not mistake prerelease labels such as -beta/-rc for
        // stable releases. Numeric expanded MC suffixes must match the selected
        // Minecraft version; -mcNNN is Forge's compact legacy notation.
        bool suffix_supported = separator == std::string::npos;
        if (separator != std::string::npos) {
            const auto suffix = candidate.substr(separator + 1);
            if (suffix.starts_with("mc")) {
                const auto compact_version = suffix.substr(2);
                suffix_supported = !compact_version.empty() &&
                    std::all_of(compact_version.begin(), compact_version.end(), [](unsigned char ch) {
                        return std::isdigit(ch);
                    });
            } else {
                const auto suffix_parts = numeric_version_parts(suffix);
                suffix_supported = !minecraft_parts.empty() &&
                    suffix_parts.size() >= minecraft_parts.size() &&
                    std::equal(minecraft_parts.begin(), minecraft_parts.end(), suffix_parts.begin());
            }
        }
        if (!suffix_supported) continue;

        if (latest_numeric_build.empty() || newer_numeric_version(numeric_build, latest_numeric_build)) {
            latest_numeric_build = numeric_build;
            latest_artifact_suffix = candidate;
        }
    }

    if (latest_artifact_suffix.empty())
        throw std::runtime_error("Forge has no stable published build for Minecraft " + minecraft_version);
    return latest_artifact_suffix;
}

nlohmann::json merge_forge_profile(const nlohmann::json& vanilla,
                                   const nlohmann::json& forge) {
    if (!vanilla.is_object() || !forge.is_object())
        throw std::runtime_error("Cannot combine invalid Minecraft and Forge metadata");

    auto merged = vanilla;
    for (const auto* key : {"id", "type", "time", "releaseTime", "mainClass",
                            "minecraftArguments", "javaVersion", "logging"}) {
        if (forge.contains(key)) merged[key] = forge[key];
    }

    // Child Forge libraries come first. Libraries inherited from Mojang are appended
    // only when the Forge manifest does not already declare the same Maven coordinate.
    nlohmann::json libraries = nlohmann::json::array();
    std::unordered_set<std::string> names;
    auto append_libraries = [&](const nlohmann::json& source) {
        if (!source.is_object() || !source.contains("libraries") || !source["libraries"].is_array()) return;
        for (const auto& library : source["libraries"]) {
            const auto name = library.is_object() ? library.value("name", std::string()) : std::string();
            const auto identity = name.empty() ? library.dump() : name;
            if (names.insert(identity).second) libraries.push_back(library);
        }
    };
    append_libraries(forge);
    append_libraries(vanilla);
    merged["libraries"] = std::move(libraries);

    // Forge launch arguments extend the inherited Mojang arguments. This preserves
    // Mojang's required JVM/game options while adding Forge's launch target and flags.
    if ((vanilla.contains("arguments") && vanilla["arguments"].is_object()) ||
        (forge.contains("arguments") && forge["arguments"].is_object())) {
        nlohmann::json arguments = nlohmann::json::object();
        auto append_arguments = [&](const nlohmann::json& source) {
            if (!source.is_object() || !source.contains("arguments") || !source["arguments"].is_object()) return;
            for (auto it = source["arguments"].begin(); it != source["arguments"].end(); ++it) {
                if (!it.value().is_array()) continue;
                if (!arguments.contains(it.key()) || !arguments[it.key()].is_array())
                    arguments[it.key()] = nlohmann::json::array();
                for (const auto& value : it.value()) arguments[it.key()].push_back(value);
            }
        };
        append_arguments(vanilla);
        append_arguments(forge);
        merged["arguments"] = std::move(arguments);
    }
    return merged;
}

}  // namespace launcher::mod_loader
