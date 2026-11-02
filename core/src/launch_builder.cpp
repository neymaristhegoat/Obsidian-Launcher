#include "launcher/launch_builder.h"

#include "launcher/accounts.h"
#include "launcher/config.h"
#include "launcher/instance.h"
#include "launcher/offline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace launcher::launch_builder {
namespace {

std::string platform_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "osx";
#else
    return "linux";
#endif
}

bool safe_identifier(const std::string& value) {
    return !value.empty() && value != "." && value != ".." && value.size() <= 100 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
           });
}

std::string path_utf8(const fs::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

fs::path safe_relative_path(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    fs::path relative(value);
    if (value.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        throw std::runtime_error("Minecraft version metadata contains an unsafe library path");
    for (const auto& part : relative) {
        const auto component = part.string();
        if (component.empty() || component == "." || component == ".." || component.find(':') != std::string::npos)
            throw std::runtime_error("Minecraft version metadata contains an unsafe library path");
    }
    return relative.lexically_normal();
}

bool rule_matches(const json& rule) {
    if (rule.contains("os") && rule["os"].is_object()) {
        const auto& os = rule["os"];
        if (os.contains("name") && os["name"].is_string() && os["name"].get<std::string>() != platform_name())
            return false;
        if (os.contains("arch") && os["arch"].is_string()) {
            std::string arch;
#if defined(__aarch64__) || defined(_M_ARM64)
            arch = "aarch64";
#elif INTPTR_MAX >= INT64_MAX
            arch = "x86_64";
#else
            arch = "x86";
#endif
            try {
                if (!std::regex_search(arch, std::regex(os["arch"].get<std::string>()))) return false;
            } catch (const std::regex_error&) { return false; }
        }
    }
    if (rule.contains("features") && rule["features"].is_object()) {
        for (auto it = rule["features"].begin(); it != rule["features"].end(); ++it)
            if (it.value().is_boolean() && it.value().get<bool>()) return false;
    }
    return true;
}

bool allowed(const json& entry) {
    if (!entry.contains("rules") || !entry["rules"].is_array()) return true;
    bool result = false;
    for (const auto& rule : entry["rules"])
        if (rule.is_object() && rule_matches(rule))
            result = rule.value("action", std::string("disallow")) == "allow";
    return result;
}

std::string coordinate_path(const std::string& coordinate) {
    auto value = coordinate;
    std::string extension = "jar";
    const auto at = value.find('@');
    if (at != std::string::npos) {
        extension = value.substr(at + 1);
        value.resize(at);
    }
    std::vector<std::string> parts;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto end = value.find(':', offset);
        parts.push_back(value.substr(offset, end == std::string::npos ? end : end - offset));
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (parts.size() < 3 || parts.size() > 4) throw std::runtime_error("Unsupported Minecraft library coordinate");
    auto group = parts[0];
    std::replace(group.begin(), group.end(), '.', '/');
    auto path = group + "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (parts.size() == 4) path += "-" + parts[3];
    return path + "." + extension;
}

std::vector<fs::path> classpath_libraries(const json& metadata, const fs::path& root) {
    std::vector<fs::path> result;
    if (!metadata.contains("libraries") || !metadata["libraries"].is_array()) return result;
    for (const auto& library : metadata["libraries"]) {
        if (!library.is_object() || !allowed(library)) continue;
        std::string relative;
        const bool has_download_metadata = library.contains("downloads") && library["downloads"].is_object();
        if (has_download_metadata && library["downloads"].contains("artifact") &&
            library["downloads"]["artifact"].is_object())
            relative = library["downloads"]["artifact"].value("path", std::string());
        if (relative.empty() && !has_download_metadata && library.contains("name") && library["name"].is_string())
            relative = coordinate_path(library["name"].get<std::string>());
        if (relative.empty()) continue;
        const auto path = root / "libraries" / safe_relative_path(relative);
        if (!fs::is_regular_file(path))
            throw std::runtime_error("A required Minecraft library is missing. Reinstall this version and try again.");
        result.push_back(path);
    }
    return result;
}

std::string classpath_join(const std::vector<fs::path>& paths) {
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string result;
    for (const auto& path : paths) {
        if (!result.empty()) result.push_back(separator);
        result += path_utf8(path);
    }
    return result;
}

void replace_all(std::string& value, const std::string& key, const std::string& replacement) {
    const std::string token = "${" + key + "}";
    std::size_t offset = 0;
    while ((offset = value.find(token, offset)) != std::string::npos) {
        value.replace(offset, token.size(), replacement);
        offset += replacement.size();
    }
}

std::string substitute(std::string value, const std::map<std::string, std::string>& values) {
    for (const auto& [key, replacement] : values) replace_all(value, key, replacement);
    if (value.find("${") != std::string::npos)
        throw std::runtime_error("This Minecraft version needs launch options that Obsidian does not support yet");
    return value;
}

void append_argument_value(std::vector<std::string>& out, const json& value,
                           const std::map<std::string, std::string>& values) {
    if (value.is_string()) out.push_back(substitute(value.get<std::string>(), values));
    else if (value.is_array())
        for (const auto& item : value)
            if (item.is_string()) out.push_back(substitute(item.get<std::string>(), values));
}

std::vector<std::string> modern_arguments(const json& metadata, const std::string& group,
                                         const std::map<std::string, std::string>& values) {
    std::vector<std::string> result;
    if (!metadata.contains("arguments") || !metadata["arguments"].is_object() ||
        !metadata["arguments"].contains(group) || !metadata["arguments"][group].is_array()) return result;
    for (const auto& item : metadata["arguments"][group]) {
        if (item.is_string()) {
            result.push_back(substitute(item.get<std::string>(), values));
            continue;
        }
        if (!item.is_object() || !allowed(item)) continue;
        if (item.contains("value")) append_argument_value(result, item["value"], values);
    }
    return result;
}

std::vector<std::string> split_legacy_arguments(const std::string& input,
                                               const std::map<std::string, std::string>& values) {
    std::vector<std::string> result;
    std::string current;
    char quote = 0;
    bool escaped = false;
    for (const char ch : input) {
        if (escaped) { current.push_back(ch); escaped = false; continue; }
        if (ch == '\\' && quote) { escaped = true; continue; }
        if ((ch == '"' || ch == '\'') && (!quote || quote == ch)) { quote = quote ? 0 : ch; continue; }
        if (std::isspace(static_cast<unsigned char>(ch)) && !quote) {
            if (!current.empty()) { result.push_back(substitute(current, values)); current.clear(); }
            continue;
        }
        current.push_back(ch);
    }
    if (quote) throw std::runtime_error("Minecraft version has malformed legacy arguments");
    if (!current.empty()) result.push_back(substitute(current, values));
    return result;
}

fs::path java_executable(const std::string& configured) {
    auto executable_name = [] {
#ifdef _WIN32
        return std::string("java.exe");
#else
        return std::string("java");
#endif
    }();
    auto usable = [](const fs::path& candidate) {
        std::error_code error;
        if (!fs::is_regular_file(candidate, error) || error) return false;
#ifdef _WIN32
        return true;
#else
        return ::access(candidate.c_str(), X_OK) == 0;
#endif
    };
    if (!configured.empty()) {
        fs::path candidate(configured);
        std::error_code error;
        if (fs::is_directory(candidate, error)) candidate /= "bin" / fs::path(executable_name);
        if (usable(candidate)) return candidate;
        throw std::runtime_error("The configured Java executable was not found or is not executable");
    }

    if (const char* home = std::getenv("JAVA_HOME")) {
        const auto candidate = fs::path(home) / "bin" / executable_name;
        if (usable(candidate)) return candidate;
    }
    if (const char* path_env = std::getenv("PATH")) {
#ifdef _WIN32
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        std::string paths(path_env);
        std::size_t offset = 0;
        while (offset <= paths.size()) {
            const auto end = paths.find(separator, offset);
            const auto directory = paths.substr(offset, end == std::string::npos ? end : end - offset);
            const auto candidate = fs::path(directory.empty() ? "." : directory) / executable_name;
            if (usable(candidate)) return candidate;
            if (end == std::string::npos) break;
            offset = end + 1;
        }
    }
#ifdef _WIN32
    std::vector<fs::path> candidates{
        fs::path("C:/Program Files/Java") / executable_name,
        fs::path("C:/Program Files/Eclipse Adoptium") / "bin" / executable_name,
        fs::path("C:/Program Files/Microsoft") / "bin" / executable_name};
    for (const auto& base : {fs::path("C:/Program Files/Java"), fs::path("C:/Program Files/Eclipse Adoptium"),
                             fs::path("C:/Program Files/Microsoft"), fs::path("C:/Program Files/Amazon Corretto")}) {
        std::error_code error;
        if (!fs::is_directory(base, error)) continue;
        for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_directory(error)) continue;
            candidates.push_back(it->path() / "bin" / executable_name);
        }
    }
#else
    std::vector<fs::path> candidates{fs::path("/usr/bin/java"), fs::path("/usr/local/bin/java"),
                                     fs::path("/opt/homebrew/opt/openjdk/bin/java"),
                                     fs::path("/usr/local/opt/openjdk/bin/java")};
    for (const auto& base : {fs::path("/usr/lib/jvm"), fs::path("/Library/Java/JavaVirtualMachines")}) {
        std::error_code error;
        if (!fs::is_directory(base, error)) continue;
        for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_directory(error)) continue;
#ifdef __APPLE__
            candidates.push_back(it->path() / "Contents/Home/bin/java");
#else
            candidates.push_back(it->path() / "bin/java");
#endif
        }
    }
#endif
    for (const auto& candidate : candidates) if (usable(candidate)) return candidate;
    throw std::runtime_error("Java was not found. Install a compatible Java runtime or select Java in Settings.");
}

json read_instance(const std::string& data_dir, const std::string& id, fs::path& instance_dir) {
    std::lock_guard<std::mutex> lock(launcher::instance::content_mutex());
    instance_dir = launcher::instance::path_for(data_dir, id);
    std::ifstream file(instance_dir / "instance.json");
    if (!file) throw std::runtime_error("Instance profile was not found");
    const auto profile = json::parse(file, nullptr, false);
    if (!profile.is_object() || profile.value("id", std::string()) != id)
        throw std::runtime_error("Instance profile is invalid");
    return profile;
}

}  // namespace

json build(const std::string& data_dir, const json& params) {
    const auto instance_id = params.at("instanceId").get<std::string>();
    if (!safe_identifier(instance_id)) throw std::runtime_error("Invalid instance identifier");
    const auto session = params.at("session");
    if (!session.is_object()) throw std::runtime_error("Sign in or choose an offline username before launching");

    fs::path instance_dir;
    const auto profile = read_instance(data_dir, instance_id, instance_dir);
    const auto version_id = profile.value("version", std::string());
    if (profile.value("loader", std::string("vanilla")) != "vanilla")
        throw std::runtime_error("This launcher can launch vanilla profiles. Mod loader installation is not available yet.");
    if (!safe_identifier(version_id)) throw std::runtime_error("Instance has an invalid Minecraft version");
    if (profile.value("installedVersion", std::string()) != version_id)
        throw std::runtime_error("Install this Minecraft version before launching it");

    const auto account_id = session.value("id", std::string());
    const auto saved_account = launcher::accounts::get(account_id);
    if (!saved_account) throw std::runtime_error("The selected account is no longer available. Sign in again.");

    const bool offline = session.value("offline", false);
    std::string username;
    std::string uuid;
    std::string access_token;
    std::string user_type;
    std::string xuid;
    if (offline) {
        if (saved_account->type != "offline" || launcher::accounts::active_id() != account_id)
            throw std::runtime_error("Select the offline profile you want to use before launching");
        username = saved_account->username;
        uuid = saved_account->uuid;
        access_token = "0";
        user_type = "legacy";
        if (!launcher::offline::valid_username(username)) throw std::runtime_error("The offline username is invalid");
    } else {
        if (saved_account->type != "microsoft" || launcher::accounts::active_id() != account_id)
            throw std::runtime_error("Select the Microsoft account you want to use before launching");
        username = session.value("username", std::string());
        uuid = session.value("uuid", std::string());
        access_token = session.value("accessToken", std::string());
        xuid = session.value("xuid", std::string());
        user_type = "msa";
        if (username.empty() || uuid.empty() || uuid != saved_account->uuid || access_token.empty() || access_token == "0")
            throw std::runtime_error("The Microsoft session is incomplete. Sign in again and retry.");
    }

    const fs::path root(data_dir);
    const auto version_dir = root / "versions" / version_id;
    const auto metadata_path = version_dir / (version_id + ".json");
    std::ifstream metadata_file(metadata_path);
    if (!metadata_file) throw std::runtime_error("Minecraft version files are missing. Reinstall this version and try again.");
    const auto metadata = json::parse(metadata_file, nullptr, false);
    if (!metadata.is_object()) throw std::runtime_error("Minecraft version metadata is invalid. Reinstall this version and try again.");
    const auto client_path = version_dir / (version_id + ".jar");
    if (!fs::is_regular_file(client_path)) throw std::runtime_error("Minecraft client files are missing. Reinstall this version and try again.");
    const auto main_class = metadata.value("mainClass", std::string());
    if (main_class.empty()) throw std::runtime_error("Minecraft version metadata has no main class");

    std::vector<fs::path> classpath = classpath_libraries(metadata, root);
    classpath.push_back(client_path);
    const auto classpath_value = classpath_join(classpath);
#ifdef _WIN32
    constexpr auto classpath_separator = ";";
#else
    constexpr auto classpath_separator = ":";
#endif
    const auto asset_index = metadata.value("assetIndex", json::object());
    std::string asset_index_name = asset_index.value("id", metadata.value("assets", version_id));
    const auto natives_dir = root / "natives" / version_id;
    const auto minecraft_dir = instance_dir / ".minecraft";

    json config = launcher::config::read();
    const json settings = config.value("settings", json::object());
    std::string configured_java = profile.value("javaPath", std::string());
    if (configured_java.empty() && settings.is_object()) configured_java = settings.value("javaPath", std::string());
    const auto java = java_executable(configured_java);

    const auto memory = profile.value("memory", settings.is_object() ? settings.value("memory", 4) : 4);
    if (memory < 1 || memory > 64) throw std::runtime_error("Instance memory allocation must be between 1 and 64 GB");
    const auto version_type = metadata.value("type", std::string("release"));
    const std::string user_properties = "{}";
    std::map<std::string, std::string> values{
        {"auth_player_name", username}, {"version_name", version_id},
        {"game_directory", path_utf8(minecraft_dir)}, {"assets_root", path_utf8(root / "assets")},
        {"assets_index_name", asset_index_name}, {"auth_uuid", uuid},
        {"auth_access_token", access_token}, {"auth_session", access_token}, {"user_type", user_type},
        {"version_type", version_type}, {"user_properties", user_properties},
        {"clientid", "6be102b8-2980-4c03-bd3b-3185c27713f4"}, {"auth_xuid", xuid},
        {"natives_directory", path_utf8(natives_dir)}, {"launcher_name", "Obsidian Launcher"},
        {"launcher_version", "0.1.0"}, {"classpath", classpath_value},
        {"library_directory", path_utf8(root / "libraries")},
        {"classpath_separator", classpath_separator}, {"resolution_width", "854"},
        {"resolution_height", "480"}, {"game_assets", path_utf8(root / "assets" / "virtual" / asset_index_name)}};

    auto jvm = modern_arguments(metadata, "jvm", values);
    if (jvm.empty()) {
        jvm = {"-Djava.library.path=" + path_utf8(natives_dir), "-cp", classpath_value};
    } else {
        const bool has_classpath = std::find(jvm.begin(), jvm.end(), "-cp") != jvm.end() ||
                                   std::find(jvm.begin(), jvm.end(), "-classpath") != jvm.end();
        if (!has_classpath) { jvm.push_back("-cp"); jvm.push_back(classpath_value); }
        const bool has_native_path = std::any_of(jvm.begin(), jvm.end(), [](const std::string& arg) {
            return arg.starts_with("-Djava.library.path=");
        });
        if (!has_native_path) jvm.push_back("-Djava.library.path=" + path_utf8(natives_dir));
    }
    jvm.erase(std::remove_if(jvm.begin(), jvm.end(), [](const std::string& arg) {
        return arg.starts_with("-Xmx");
    }), jvm.end());
    jvm.insert(jvm.begin(), "-Xmx" + std::to_string(memory) + "G");

    auto game = modern_arguments(metadata, "game", values);
    if (game.empty() && metadata.contains("minecraftArguments") && metadata["minecraftArguments"].is_string())
        game = split_legacy_arguments(metadata["minecraftArguments"].get<std::string>(), values);
    if (game.empty()) throw std::runtime_error("Minecraft version has no supported game arguments");

    std::vector<std::string> arguments = std::move(jvm);
    arguments.push_back(main_class);
    arguments.insert(arguments.end(), game.begin(), game.end());
    return {{"instanceId", instance_id}, {"version", version_id}, {"username", username},
            {"javaPath", path_utf8(java)}, {"workingDirectory", path_utf8(minecraft_dir)},
            {"arguments", std::move(arguments)}};
}

}  // namespace launcher::launch_builder
