#include "launcher/modrinth.h"
#include "launcher/instance.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace launcher::modrinth {

namespace {

constexpr auto kApi = "https://api.modrinth.com/v2";
constexpr auto kUserAgent = "ObsidianLauncher/0.1.0";

bool valid_identifier(const std::string& value) {
    return !value.empty() && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isalnum(c) || c == '-' || c == '_';
           });
}

std::string content_directory(const std::string& type) {
    if (type == "mod") return "mods";
    if (type == "resourcepack") return "resourcepacks";
    if (type == "shader") return "shaderpacks";
    throw std::runtime_error("Unsupported Modrinth project type");
}

std::string safe_filename(std::string filename) {
    if (filename.empty() || filename.size() > 240)
        throw std::runtime_error("Modrinth returned an invalid filename");
    for (char& c : filename) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    while (!filename.empty() && (filename.front() == '.' || filename.front() == ' '))
        filename.erase(filename.begin());
    if (filename.empty()) throw std::runtime_error("Modrinth returned an invalid filename");
    return filename;
}

fs::path instance_root(const std::string& data_dir, const std::string& instance_id) {
    if (!valid_identifier(instance_id)) throw std::runtime_error("Invalid instance identifier");
    return launcher::instance::path_for(data_dir, instance_id);
}

fs::path database_path(const std::string& data_dir, const std::string& instance_id) {
    return instance_root(data_dir, instance_id) / ".minecraft" / "modrinth-installed.json";
}

json read_database(const fs::path& path) {
    std::ifstream file(path);
    if (!file) return {{"items", json::array()}};
    json database = json::parse(file, nullptr, false);
    if (!database.is_object() || !database.contains("items") || !database["items"].is_array())
        return {{"items", json::array()}};
    return database;
}

void write_database(const fs::path& path, const json& database) {
    fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write the Modrinth install list");
        file << database.dump(2);
        if (!file) throw std::runtime_error("Cannot save the Modrinth install list");
    }
    std::error_code ec;
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
        if (ec) {
            fs::remove(temporary);
            throw std::runtime_error("Cannot update the Modrinth install list: " + ec.message());
        }
    }
}

json get_json(const std::string& url, const cpr::Parameters& parameters = {}) {
    const auto response = cpr::Get(
        cpr::Url{url}, parameters,
        cpr::Header{{"Accept", "application/json"}, {"User-Agent", kUserAgent}},
        cpr::Timeout{20000});
    if (response.error) throw std::runtime_error("Modrinth network error: " + response.error.message);
    if (response.status_code != 200)
        throw std::runtime_error("Modrinth request failed (HTTP " + std::to_string(response.status_code) + ")");
    auto result = json::parse(response.text, nullptr, false);
    if (result.is_discarded()) throw std::runtime_error("Modrinth returned invalid JSON");
    return result;
}

std::string sha1_file(const fs::path& path) {
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha1(), nullptr) != 1)
        throw std::runtime_error("Could not verify downloaded file");

    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not read downloaded file for verification");
    std::array<char, 64 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = file.gcount();
        if (count > 0 && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1)
            throw std::runtime_error("Could not verify downloaded file");
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1)
        throw std::runtime_error("Could not verify downloaded file");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) output << std::setw(2) << static_cast<int>(digest[i]);
    return output.str();
}

json install_one(const std::string& data_dir, const std::string& instance_id,
                 const std::string& content_type, const std::string& project_id,
                 const json& project_info, const std::string& game_version,
                 const std::string& loader) {
    cpr::Parameters versions_parameters{{"game_versions", json::array({game_version}).dump()},
                                        {"include_changelog", "false"}};
    if (content_type == "mod" && loader != "vanilla")
        versions_parameters.Add({"loaders", json::array({loader}).dump()});

    const auto versions = get_json(std::string(kApi) + "/project/" + project_id + "/version",
                                   versions_parameters);
    if (!versions.is_array() || versions.empty())
        throw std::runtime_error("No compatible Modrinth version was found for this profile");

    const auto& selected = versions.front();
    const auto& files = selected.at("files");
    if (!files.is_array() || files.empty()) throw std::runtime_error("This Modrinth version has no downloadable file");
    const json* primary = nullptr;
    for (const auto& candidate : files) {
        if (candidate.is_object() && candidate.value("primary", false)) { primary = &candidate; break; }
    }
    if (!primary) primary = &files.front();

    const auto url = primary->at("url").get<std::string>();
    if (!url.starts_with("https://cdn.modrinth.com/"))
        throw std::runtime_error("Modrinth returned an unexpected download host");
    const auto filename = safe_filename(primary->at("filename").get<std::string>());
    const auto hashes = primary->value("hashes", json::object());
    const auto expected_sha1 = hashes.is_object() ? hashes.value("sha1", std::string()) : std::string();

    const auto profile = instance_root(data_dir, instance_id);
    const auto directory = profile / ".minecraft" / content_directory(content_type);
    fs::create_directories(directory);
    fs::path destination = directory / filename;
    fs::path temporary = destination;
    temporary += ".part";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot create the Modrinth download file");
        const auto response = cpr::Download(
            output, cpr::Url{url},
            cpr::Header{{"User-Agent", kUserAgent}}, cpr::Timeout{120000});
        output.close();
        if (response.error || response.status_code != 200) {
            std::error_code ec;
            fs::remove(temporary, ec);
            throw std::runtime_error(response.error
                ? "Modrinth download failed: " + response.error.message
                : "Modrinth download failed (HTTP " + std::to_string(response.status_code) + ")");
        }
    }

    const auto actual_sha1 = sha1_file(temporary);
    if (!expected_sha1.empty() && actual_sha1 != expected_sha1) {
        std::error_code ec;
        fs::remove(temporary, ec);
        throw std::runtime_error("Downloaded file failed its SHA-1 check");
    }

    const auto metadata = database_path(data_dir, instance_id);
    json database = read_database(metadata);
    auto items = database.at("items");
    const auto existing = std::find_if(items.begin(), items.end(), [&](const json& item) {
        return item.value("projectId", std::string()) == project_id &&
               item.value("projectType", std::string()) == content_type;
    });
    const bool replaces_same_file = existing != items.end() &&
                                    existing->value("filename", std::string()) == filename;
    if (existing != items.end()) {
        const auto old_filename = existing->value("filename", std::string());
        if (old_filename != filename) {
            const auto old_path = directory / safe_filename(old_filename);
            std::error_code ec;
            fs::remove(old_path, ec);
        }
        items.erase(existing);
    }
    if (fs::exists(destination) && !replaces_same_file) {
        const auto stem = destination.stem().string() + "-" + project_id;
        destination = directory / (stem + destination.extension().string());
    }

    std::error_code rename_error;
    fs::rename(temporary, destination, rename_error);
    if (rename_error) {
        fs::remove(destination, rename_error);
        rename_error.clear();
        fs::rename(temporary, destination, rename_error);
        if (rename_error) {
            fs::remove(temporary);
            throw std::runtime_error("Cannot move the downloaded file into the instance folder");
        }
    }

    const auto installed_at = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    json item = {{"projectId", project_id},
                 {"projectType", content_type},
                 {"title", project_info.value("title", project_id)},
                 {"author", project_info.value("author", std::string())},
                 {"description", project_info.value("description", std::string())},
                 {"iconUrl", project_info.value("icon_url", std::string())},
                 {"filename", destination.filename().string()},
                 {"versionId", selected.value("id", std::string())},
                 {"versionNumber", selected.value("version_number", std::string())},
                 {"sha1", actual_sha1},
                 {"installedAt", installed_at}};
    items.push_back(item);
    database["items"] = std::move(items);
    write_database(metadata, database);
    return item;
}

}  // namespace

json search(const json& params) {
    const auto project_type = params.at("projectType").get<std::string>();
    content_directory(project_type);
    const auto game_version = params.at("minecraftVersion").get<std::string>();
    json facets = json::array({json::array({"project_type:" + project_type}),
                               json::array({"versions:" + game_version})});
    if (project_type == "mod") {
        const auto loader = params.value("loader", std::string("vanilla"));
        if (loader != "vanilla") facets.push_back(json::array({"categories:" + loader}));
    }

    cpr::Parameters query{{"facets", facets.dump()}, {"index", "downloads"}, {"limit", "24"}};
    const auto search_term = params.value("query", std::string());
    if (!search_term.empty()) query.Add({"query", search_term});
    const auto result = get_json(std::string(kApi) + "/search", query);
    if (!result.is_object() || !result.contains("hits") || !result["hits"].is_array())
        throw std::runtime_error("Modrinth returned an invalid search response");
    return {{"hits", result["hits"]}, {"totalHits", result.value("total_hits", 0)}};
}

json installed(const std::string& data_dir, const json& params) {
    const auto id = params.at("instanceId").get<std::string>();
    std::lock_guard<std::mutex> lock(launcher::instance::content_mutex());
    return read_database(database_path(data_dir, id));
}

json install(const std::string& data_dir, const json& params) {
    const std::string type = params.at("projectType").get<std::string>();
    content_directory(type);
    const std::string project_id = params.at("projectId").get<std::string>();
    if (!valid_identifier(project_id)) throw std::runtime_error("Invalid Modrinth project identifier");
    const std::string game_version = params.at("minecraftVersion").get<std::string>();
    if (game_version.empty() || game_version.size() > 32) throw std::runtime_error("Invalid Minecraft version");
    const std::string loader = params.value("loader", std::string("vanilla"));

    std::lock_guard<std::mutex> lock(launcher::instance::content_mutex());
    const auto project_info = get_json(std::string(kApi) + "/project/" + project_id);
    if (project_info.value("project_type", type) != type)
        throw std::runtime_error("This Modrinth project does not match the selected tab");
    const auto instance_id = params.at("instanceId").get<std::string>();
    return install_one(data_dir, instance_id, type, project_id, project_info, game_version, loader);
}

json remove(const std::string& data_dir, const json& params) {
    const std::string type = params.at("projectType").get<std::string>();
    const auto directory = content_directory(type);
    const std::string project_id = params.at("projectId").get<std::string>();
    if (!valid_identifier(project_id)) throw std::runtime_error("Invalid Modrinth project identifier");
    const auto id = params.at("instanceId").get<std::string>();

    std::lock_guard<std::mutex> lock(launcher::instance::content_mutex());
    const auto metadata = database_path(data_dir, id);
    auto database = read_database(metadata);
    auto items = database.at("items");
    bool removed = false;
    for (auto it = items.begin(); it != items.end();) {
        if (it->value("projectId", std::string()) == project_id &&
            it->value("projectType", std::string()) == type) {
            const auto filename = it->value("filename", std::string());
            if (!filename.empty()) {
                std::error_code ec;
                fs::remove(instance_root(data_dir, id) / ".minecraft" / directory / safe_filename(filename), ec);
            }
            it = items.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) {
        database["items"] = std::move(items);
        write_database(metadata, database);
    }
    return {{"ok", removed}};
}

}  // namespace launcher::modrinth
