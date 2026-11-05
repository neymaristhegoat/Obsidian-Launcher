#include "launcher/minecraft_installer.h"

#include "launcher/instance.h"
#include "launcher/config.h"
#include "launcher/mod_loader.h"
#include "launcher/version_manifest.h"

#include <cpr/cpr.h>
#include <openssl/evp.h>
#include <zip.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <cstring>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace launcher::minecraft_installer {
namespace {

constexpr auto kUserAgent = "Obsidian Launcher/0.1.0";
constexpr int kWorkerCount = 8;
std::mutex g_install_mutex;

struct DownloadItem {
    std::string url;
    fs::path destination;
    std::string sha1;
    std::uint64_t size = 0;
};

struct NativeArchive {
    fs::path path;
    std::vector<std::string> excluded;
};

struct InstallContext {
    std::shared_ptr<std::atomic_bool> cancelled;
    Progress progress;

    void check_cancelled() const {
        if (cancelled && cancelled->load()) throw std::runtime_error("Installation cancelled");
    }

    void report(const std::string& message, int percent, const std::string& state = "installing") const {
        if (progress) progress(message, std::clamp(percent, 0, 100), state);
    }
};

std::string platform_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "osx";
#else
    return "linux";
#endif
}

std::string platform_arch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif INTPTR_MAX >= INT64_MAX
    return "x86_64";
#else
    return "x86";
#endif
}

std::string native_arch_token() {
    return platform_arch() == "x86" ? "32" : "64";
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool safe_identifier(const std::string& value) {
    return !value.empty() && value != "." && value != ".." && value.size() <= 100 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
           });
}

fs::path safe_relative_path(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    fs::path relative(value);
    if (value.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        throw std::runtime_error("Minecraft metadata contains an unsafe file path");
    for (const auto& part : relative) {
        const auto component = part.string();
        if (component.empty() || component == "." || component == ".." || component.find(':') != std::string::npos)
            throw std::runtime_error("Minecraft metadata contains an unsafe file path");
    }
    return relative.lexically_normal();
}

void ensure_no_symlink_components(const fs::path& root, const fs::path& relative) {
    fs::path current = root;
    std::error_code error;
    for (const auto& part : relative) {
        current /= part;
        const auto status = fs::symlink_status(current, error);
        if (!error && fs::is_symlink(status))
            throw std::runtime_error("Refusing to install through a symbolic link");
        error.clear();
    }
}

std::string sha1_string(std::string_view bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha1(), nullptr) != 1)
        throw std::runtime_error("Could not verify Minecraft download");
    std::string result;
    result.reserve(length * 2);
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < length; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}

std::string sha1_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) throw std::runtime_error("Could not verify Minecraft download");
    const auto cleanup = [](EVP_MD_CTX* value) { EVP_MD_CTX_free(value); };
    std::unique_ptr<EVP_MD_CTX, decltype(cleanup)> holder(context, cleanup);
    if (EVP_DigestInit_ex(context, EVP_sha1(), nullptr) != 1)
        throw std::runtime_error("Could not verify Minecraft download");
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1)
            throw std::runtime_error("Could not verify Minecraft download");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context, digest.data(), &length) != 1)
        throw std::runtime_error("Could not verify Minecraft download");
    std::string result;
    result.reserve(length * 2);
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < length; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}

bool file_matches(const fs::path& path, const std::string& expected_sha1, std::uint64_t expected_size) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) return false;
    if (expected_size && fs::file_size(path, error) != expected_size) return false;
    if (!expected_sha1.empty() && lower(sha1_file(path)) != lower(expected_sha1)) return false;
    return !expected_sha1.empty() || expected_size > 0;
}

void replace_file(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    fs::rename(source, destination, error);
    if (!error) return;
    fs::remove(destination, error);
    error.clear();
    fs::rename(source, destination, error);
    if (error) {
        fs::remove(source);
        throw std::runtime_error("Cannot install downloaded file: " + error.message());
    }
}

std::string fetch_text(const std::string& url, const InstallContext& context) {
    context.check_cancelled();
    if (!url.starts_with("https://")) throw std::runtime_error("Minecraft metadata returned a non-HTTPS URL");
    cpr::ProgressCallback callback([&context](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t,
                                               cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
        return !(context.cancelled && context.cancelled->load());
    });
    const auto response = cpr::Get(cpr::Url{url},
                                   cpr::Header{{"Accept", "application/json"}, {"User-Agent", kUserAgent}},
                                   cpr::Timeout{60000}, callback);
    context.check_cancelled();
    if (response.error) throw std::runtime_error("Minecraft metadata request failed: " + response.error.message);
    if (response.status_code != 200)
        throw std::runtime_error("Minecraft metadata request failed (HTTP " + std::to_string(response.status_code) + ")");
    return response.text;
}

bool os_rule_matches(const json& rule) {
    if (rule.contains("os") && rule["os"].is_object()) {
        const auto& os = rule["os"];
        if (os.contains("name") && os["name"].is_string() && os["name"].get<std::string>() != platform_name())
            return false;
        if (os.contains("arch") && os["arch"].is_string()) {
            try {
                if (!std::regex_search(platform_arch(), std::regex(os["arch"].get<std::string>()))) return false;
            } catch (const std::regex_error&) { return false; }
        }
    }
    if (rule.contains("features") && rule["features"].is_object()) {
        for (auto it = rule["features"].begin(); it != rule["features"].end(); ++it)
            if (it.value().is_boolean() && it.value().get<bool>()) return false;
    }
    return true;
}

bool library_allowed(const json& library) {
    if (!library.contains("rules") || !library["rules"].is_array()) return true;
    bool allowed = false;
    for (const auto& rule : library["rules"]) {
        if (!rule.is_object() || !os_rule_matches(rule)) continue;
        allowed = rule.value("action", std::string("disallow")) == "allow";
    }
    return allowed;
}

std::string coordinate_path(const std::string& coordinate) {
    std::string value = coordinate;
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
    std::string group = parts[0];
    std::replace(group.begin(), group.end(), '.', '/');
    std::string path = group + "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (parts.size() == 4) path += "-" + parts[3];
    path += "." + extension;
    return path;
}

std::string library_base_url(const json& library) {
    std::string base = library.value("url", std::string("https://libraries.minecraft.net/"));
    if (!base.starts_with("https://")) throw std::runtime_error("Minecraft library metadata returned a non-HTTPS URL");
    if (!base.empty() && base.back() != '/') base.push_back('/');
    return base;
}

std::vector<std::string> exclusions(const json& library) {
    std::vector<std::string> result{"META-INF/"};
    if (library.contains("extract") && library["extract"].is_object() &&
        library["extract"].contains("exclude") && library["extract"]["exclude"].is_array()) {
        for (const auto& entry : library["extract"]["exclude"])
            if (entry.is_string()) result.push_back(entry.get<std::string>());
    }
    return result;
}

bool should_extract(const std::string& entry, const std::vector<std::string>& excluded) {
    for (const auto& prefix : excluded)
        if (entry.starts_with(prefix)) return false;
    return true;
}

void add_download(std::map<std::string, DownloadItem>& plan, DownloadItem item) {
    if (!item.url.starts_with("https://")) throw std::runtime_error("Minecraft metadata returned a non-HTTPS download URL");
    const auto key = item.destination.generic_string();
    const auto found = plan.find(key);
    if (found == plan.end()) plan.emplace(key, std::move(item));
    else if (!found->second.sha1.empty() && !item.sha1.empty() && lower(found->second.sha1) != lower(item.sha1))
        throw std::runtime_error("Minecraft metadata contains conflicting library checksums");
}

fs::path artifact_destination(const json& artifact, const fs::path& install_root) {
    if (!artifact.is_object() || !artifact.contains("path") || !artifact["path"].is_string())
        throw std::runtime_error("Minecraft library metadata is missing an artifact path");
    const auto relative = safe_relative_path(artifact.at("path").get<std::string>());
    ensure_no_symlink_components(install_root / "libraries", relative);
    return install_root / "libraries" / relative;
}

DownloadItem artifact_from(const json& artifact, const fs::path& install_root) {
    if (!artifact.is_object() || !artifact.contains("url") || !artifact["url"].is_string() ||
        artifact["url"].get<std::string>().empty())
        throw std::runtime_error("Minecraft library metadata is missing an artifact download URL");
    return {artifact.at("url").get<std::string>(), artifact_destination(artifact, install_root),
            artifact.value("sha1", std::string()), artifact.value("size", std::uint64_t{0})};
}

void prepare_profile_artifact(const json& artifact, const fs::path& install_root,
                              std::map<std::string, DownloadItem>& plan) {
    const auto destination = artifact_destination(artifact, install_root);
    if (artifact.contains("url") && artifact["url"].is_string() &&
        !artifact["url"].get<std::string>().empty()) {
        add_download(plan, artifact_from(artifact, install_root));
        return;
    }

    // Forge's generated version manifests commonly contain path-only artifact
    // records. The official Forge installer has already downloaded or generated
    // those files, so reuse them rather than inventing a Maven repository URL.
    if (!fs::is_regular_file(destination)) {
        throw std::runtime_error("Forge's installer did not create a required library: " +
                                 destination.string());
    }
    const auto expected_sha1 = artifact.value("sha1", std::string());
    const auto expected_size = artifact.value("size", std::uint64_t{0});
    if ((!expected_sha1.empty() || expected_size > 0) &&
        !file_matches(destination, expected_sha1, expected_size)) {
        throw std::runtime_error("A library created by Forge's installer failed its integrity check: " +
                                 destination.string());
    }
}

void prepare_coordinate_library(const json& library, const std::string& coordinate,
                                const fs::path& install_root,
                                std::vector<NativeArchive>& natives,
                                std::map<std::string, DownloadItem>& plan,
                                bool reuse_existing_libraries,
                                bool include_main, bool include_native) {
    if (coordinate.empty()) return;
    const auto base = [&]() { return library_base_url(library); };
    auto prepare_coordinate_file = [&](const std::string& artifact_coordinate,
                                       const fs::path& destination) {
        if (reuse_existing_libraries && fs::is_regular_file(destination)) return;
        if (reuse_existing_libraries && !library.contains("url"))
            throw std::runtime_error("Forge's installer did not create a required library: " +
                                     destination.string());
        const auto relative_url = coordinate_path(artifact_coordinate);
        add_download(plan, {base() + relative_url, destination, {}, 0});
    };

    if (include_main) {
        const auto relative = safe_relative_path(coordinate_path(coordinate));
        ensure_no_symlink_components(install_root / "libraries", relative);
        prepare_coordinate_file(coordinate, install_root / "libraries" / relative);
    }

    if (include_native && library.contains("natives") && library["natives"].is_object() &&
        library["natives"].contains(platform_name())) {
        auto classifier = library["natives"].at(platform_name()).get<std::string>();
        const std::string token = "${arch}";
        for (auto position = classifier.find(token); position != std::string::npos;
             position = classifier.find(token, position + native_arch_token().size()))
            classifier.replace(position, token.size(), native_arch_token());
        const auto native_coordinate = coordinate + ":" + classifier;
        const auto relative = safe_relative_path(coordinate_path(native_coordinate));
        ensure_no_symlink_components(install_root / "libraries", relative);
        const auto destination = install_root / "libraries" / relative;
        natives.push_back({destination, exclusions(library)});
        prepare_coordinate_file(native_coordinate, destination);
    }
}

std::vector<DownloadItem> prepare_libraries(const json& metadata, const fs::path& install_root,
                                             std::vector<NativeArchive>& natives,
                                             bool reuse_existing_libraries = false) {
    std::map<std::string, DownloadItem> plan;
    if (!metadata.contains("libraries") || !metadata["libraries"].is_array()) return {};
    for (const auto& library : metadata["libraries"]) {
        if (!library.is_object() || !library_allowed(library)) continue;
        const auto coordinate = library.value("name", std::string());
        const bool has_downloads = library.contains("downloads") && library["downloads"].is_object();
        bool artifact_prepared = false;
        bool native_prepared = false;
        bool has_classifiers = false;
        if (has_downloads) {
            const auto& downloads = library["downloads"];
            if (downloads.contains("artifact") && downloads["artifact"].is_object()) {
                prepare_profile_artifact(downloads["artifact"], install_root, plan);
                artifact_prepared = true;
            }
            has_classifiers = downloads.contains("classifiers") && downloads["classifiers"].is_object();
            if (library.contains("natives") && library["natives"].is_object() &&
                library["natives"].contains(platform_name()) && has_classifiers) {
                auto classifier = library["natives"].at(platform_name()).get<std::string>();
                const std::string token = "${arch}";
                for (auto position = classifier.find(token); position != std::string::npos;
                     position = classifier.find(token, position + native_arch_token().size()))
                    classifier.replace(position, token.size(), native_arch_token());
                const auto& classifiers = downloads["classifiers"];
                if (classifiers.contains(classifier)) {
                    const auto& native_artifact = classifiers.at(classifier);
                    const auto native_path = artifact_destination(native_artifact, install_root);
                    natives.push_back({native_path, exclusions(library)});
                    prepare_profile_artifact(native_artifact, install_root, plan);
                    native_prepared = true;
                }
            }
        }

        // Vanilla/Fabric manifests keep their original downloads behavior. Forge
        // sometimes has an empty downloads object and expects its installed Maven
        // coordinate to be used instead; do not mistake classifier-only libraries
        // for regular classpath artifacts.
        const bool coordinate_main_fallback = !artifact_prepared && !has_downloads && !coordinate.empty();
        const bool forge_main_fallback = reuse_existing_libraries && !artifact_prepared &&
                                         has_downloads && !has_classifiers && !coordinate.empty();
        const bool coordinate_native_fallback = !native_prepared && !has_downloads && !coordinate.empty();
        const bool forge_native_fallback = reuse_existing_libraries && !native_prepared &&
                                           has_downloads && !has_classifiers && !coordinate.empty();
        if (coordinate_main_fallback || forge_main_fallback ||
            coordinate_native_fallback || forge_native_fallback) {
            prepare_coordinate_library(library, coordinate, install_root, natives, plan,
                                       reuse_existing_libraries,
                                       coordinate_main_fallback || forge_main_fallback,
                                       coordinate_native_fallback || forge_native_fallback);
        }
    }
    std::vector<DownloadItem> result;
    result.reserve(plan.size());
    for (auto& [_, item] : plan) result.push_back(std::move(item));
    return result;
}

std::vector<DownloadItem> prepare_assets(const json& index, const fs::path& install_root) {
    if (!index.contains("objects") || !index["objects"].is_object())
        throw std::runtime_error("Minecraft asset index does not contain an object list");
    std::map<std::string, DownloadItem> plan;
    for (auto it = index["objects"].begin(); it != index["objects"].end(); ++it) {
        const auto& object = it.value();
        if (!object.is_object()) continue;
        const auto hash = lower(object.value("hash", std::string()));
        if (hash.size() != 40 || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c); }))
            throw std::runtime_error("Minecraft asset index contains an invalid SHA-1");
        const auto relative = fs::path("objects") / hash.substr(0, 2) / hash;
        ensure_no_symlink_components(install_root / "assets", relative);
        add_download(plan, {"https://resources.download.minecraft.net/" + hash.substr(0, 2) + "/" + hash,
                            install_root / "assets" / relative, hash,
                            object.value("size", std::uint64_t{0})});
    }
    std::vector<DownloadItem> result;
    result.reserve(plan.size());
    for (auto& [_, item] : plan) result.push_back(std::move(item));
    return result;
}

void download_one(const DownloadItem& item, const InstallContext& context) {
    context.check_cancelled();
    if (file_matches(item.destination, item.sha1, item.size)) return;
    fs::create_directories(item.destination.parent_path());
    const auto temp = fs::path(item.destination.string() + ".part-" +
                               std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create Minecraft download file");
    cpr::ProgressCallback callback([&context](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t,
                                               cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
        return !(context.cancelled && context.cancelled->load());
    });
    const auto response = cpr::Download(output,
                                        cpr::Url{item.url},
                                        cpr::Header{{"User-Agent", kUserAgent}},
                                        cpr::Timeout{120000}, callback);
    output.flush();
    output.close();
    if (response.error || response.status_code != 200) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        context.check_cancelled();
        if (response.error) throw std::runtime_error("Minecraft download failed: " + response.error.message);
        throw std::runtime_error("Minecraft download failed (HTTP " + std::to_string(response.status_code) + ")");
    }
    if (item.size && fs::file_size(temp) != item.size) {
        fs::remove(temp);
        throw std::runtime_error("Minecraft download size did not match its manifest");
    }
    const auto actual = sha1_file(temp);
    if (!item.sha1.empty() && lower(actual) != lower(item.sha1)) {
        fs::remove(temp);
        throw std::runtime_error("Minecraft download failed its SHA-1 check");
    }
    context.check_cancelled();
    replace_file(temp, item.destination);
}

void download_group(const std::vector<DownloadItem>& items, const InstallContext& context,
                    const std::string& label, int start_percent, int span_percent) {
    context.check_cancelled();
    if (items.empty()) {
        context.report(label + ": nothing to download", start_percent + span_percent);
        return;
    }
    context.report("Downloading " + label + " (" + std::to_string(items.size()) + " files)", start_percent);
    std::atomic_size_t next{0};
    std::atomic_size_t completed{0};
    std::atomic_bool failed{false};
    std::exception_ptr failure;
    std::mutex failure_mutex;
    std::mutex report_mutex;
    std::size_t last_reported = 0;
    const std::size_t report_step = std::max<std::size_t>(1, items.size() / 60);
    const int workers_count = std::min<int>(kWorkerCount, static_cast<int>(items.size()));
    std::vector<std::thread> workers;
    workers.reserve(workers_count);
    for (int worker = 0; worker < workers_count; ++worker) {
        workers.emplace_back([&] {
            while (!failed.load() && !(context.cancelled && context.cancelled->load())) {
                const auto index = next.fetch_add(1);
                if (index >= items.size()) break;
                try {
                    download_one(items[index], context);
                    completed.fetch_add(1);
                    std::lock_guard<std::mutex> report_lock(report_mutex);
                    const auto current = completed.load();
                    if (current > last_reported &&
                        (current == items.size() || current - last_reported >= report_step)) {
                        last_reported = current;
                        const auto percent = start_percent + static_cast<int>((current * span_percent) / items.size());
                        context.report(label + ": " + std::to_string(current) + " / " +
                                           std::to_string(items.size()) + " files",
                                       percent);
                    }
                } catch (...) {
                    failed = true;
                    std::lock_guard<std::mutex> failure_lock(failure_mutex);
                    if (!failure) failure = std::current_exception();
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    context.check_cancelled();
    if (failure) std::rethrow_exception(failure);
    context.report(label + " complete", start_percent + span_percent);
}

void extract_native_archive(const NativeArchive& native, const fs::path& destination,
                            const InstallContext& context) {
    context.check_cancelled();
    int error_code = 0;
    zip_t* archive = zip_open(native.path.string().c_str(), ZIP_RDONLY, &error_code);
    if (!archive) throw std::runtime_error("Could not open Minecraft native library archive");
    const auto close_archive = [](zip_t* value) { zip_close(value); };
    std::unique_ptr<zip_t, decltype(close_archive)> holder(archive, close_archive);
    const auto count = zip_get_num_entries(archive, 0);
    if (count < 0) throw std::runtime_error("Could not read Minecraft native library archive");
    for (zip_int64_t i = 0; i < count; ++i) {
        context.check_cancelled();
        const char* raw_name = zip_get_name(archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
        if (!raw_name) continue;
        const std::string name(raw_name);
        if (name.empty() || name.back() == '/' || !should_extract(name, native.excluded)) continue;
        fs::path relative;
        try { relative = safe_relative_path(name); }
        catch (...) { throw std::runtime_error("Native library contains an unsafe archive path"); }
        ensure_no_symlink_components(destination, relative);
        zip_file_t* entry = zip_fopen_index(archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
        if (!entry) throw std::runtime_error("Could not extract Minecraft native library");
        const auto close_entry = [](zip_file_t* value) { zip_fclose(value); };
        std::unique_ptr<zip_file_t, decltype(close_entry)> entry_holder(entry, close_entry);
        const auto output_path = destination / relative;
        fs::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write extracted Minecraft native library");
        std::array<char, 64 * 1024> buffer{};
        zip_int64_t read = 0;
        while ((read = zip_fread(entry, buffer.data(), buffer.size())) > 0) {
            output.write(buffer.data(), static_cast<std::streamsize>(read));
            if (!output) throw std::runtime_error("Cannot save extracted Minecraft native library");
        }
        if (read < 0) throw std::runtime_error("Could not extract Minecraft native library");
    }
}

json load_instance(const std::string& data_dir, const std::string& instance_id, fs::path& profile_path) {
    std::lock_guard<std::mutex> lock(launcher::instance::content_mutex());
    profile_path = launcher::instance::path_for(data_dir, instance_id);
    std::ifstream input(profile_path / "instance.json");
    if (!input) throw std::runtime_error("Instance profile was not found");
    auto profile = json::parse(input, nullptr, false);
    if (!profile.is_object() || profile.value("id", std::string()) != instance_id)
        throw std::runtime_error("Instance profile is invalid");
    return profile;
}
 

fs::path installer_java_executable(const json& profile) {
    auto configured = profile.value("javaPath", std::string());
    if (configured.empty()) {
        const auto config = launcher::config::read();
        const auto settings = config.value("settings", json::object());
        if (settings.is_object()) configured = settings.value("javaPath", std::string());
    }
#ifdef _WIN32
    const std::string executable_name = "java.exe";
#else
    const std::string executable_name = "java";
#endif
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
        if (fs::is_directory(candidate, error)) candidate /= fs::path("bin") / executable_name;
        if (usable(candidate)) return candidate;
        throw std::runtime_error("The configured Java executable was not found or is not executable. Select a Java runtime in launcher settings.");
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
        const std::string paths(path_env);
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
#ifndef _WIN32
    for (const auto& candidate : {fs::path("/usr/bin/java"), fs::path("/usr/local/bin/java")})
        if (usable(candidate)) return candidate;
#endif
    throw std::runtime_error("Java was not found. Install a compatible Java runtime or select one in launcher settings before installing Forge.");
}

std::string read_log_tail(const fs::path& path, std::size_t max_bytes = 6000) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size > static_cast<std::streamoff>(max_bytes)) input.seekg(-static_cast<std::streamoff>(max_bytes), std::ios::end);
    else input.seekg(0, std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

#ifdef _WIN32
std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
#endif

int run_forge_installer(const fs::path& java, const fs::path& installer,
                        const fs::path& target, const fs::path& log_path,
                        const InstallContext& context) {
    context.check_cancelled();
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE log_handle = CreateFileW(log_path.wstring().c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create Forge installer log file");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log_handle;
    startup.hStdError = log_handle;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring command = quote_windows_argument(java.wstring()) + L" -jar " +
                           quote_windows_argument(installer.wstring()) + L" --installClient " +
                           quote_windows_argument(target.wstring());
    const auto working_directory = target.wstring();
    const BOOL created = CreateProcessW(java.wstring().c_str(), command.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr, working_directory.c_str(),
                                        &startup, &process);
    const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(log_handle);
    if (!created)
        throw std::runtime_error("Could not start Forge's official installer (Windows error " +
                                 std::to_string(create_error) + ")");
    DWORD wait_result = WAIT_TIMEOUT;
    while (wait_result == WAIT_TIMEOUT) {
        if (context.cancelled && context.cancelled->load()) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            throw std::runtime_error("Installation cancelled");
        }
        wait_result = WaitForSingleObject(process.hProcess, 250);
        if (wait_result == WAIT_FAILED) {
            const DWORD wait_error = GetLastError();
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            throw std::runtime_error("Could not wait for Forge's installer (Windows error " +
                                     std::to_string(wait_error) + ")");
        }
    }
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        const DWORD exit_error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Could not read Forge installer exit code (Windows error " +
                                 std::to_string(exit_error) + ")");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
#else
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        throw std::runtime_error("Could not prepare Forge installer process");
    const auto destroy_actions = [&] { posix_spawn_file_actions_destroy(&actions); };
    if (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log_path.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO) != 0) {
        destroy_actions();
        throw std::runtime_error("Could not redirect Forge installer output to its log file");
    }
    std::vector<std::string> arguments{java.string(), "-jar", installer.string(),
                                       "--installClient", target.string()};
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t process = -1;
    const int spawn_error = posix_spawn(&process, java.c_str(), &actions, nullptr, argv.data(), environ);
    destroy_actions();
    if (spawn_error != 0)
        throw std::runtime_error("Could not start Forge's official installer: " + std::string(std::strerror(spawn_error)));
    int status = 0;
    for (;;) {
        const auto waited = waitpid(process, &status, WNOHANG);
        if (waited == process) break;
        if (waited < 0 && errno != EINTR) {
            kill(process, SIGTERM);
            waitpid(process, &status, 0);
            throw std::runtime_error("Could not wait for Forge's installer process");
        }
        if (context.cancelled && context.cancelled->load()) {
            kill(process, SIGTERM);
            waitpid(process, &status, 0);
            throw std::runtime_error("Installation cancelled");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

std::pair<std::string, json> find_forge_profile(const fs::path& root,
                                                 const std::string& minecraft_version,
                                                 const std::string& forge_build) {
    const auto versions_dir = root / "versions";
    std::error_code error;
    for (fs::directory_iterator it(versions_dir, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_directory(error)) continue;
        const auto profile_id = it->path().filename().string();
        auto normalized_profile_id = profile_id;
        std::transform(normalized_profile_id.begin(), normalized_profile_id.end(),
                       normalized_profile_id.begin(), [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        // Legacy Forge profile IDs often omit the trailing Minecraft-version
        // suffix from the Maven coordinate (for example, the artifact may be
        // 10.13.4.1614-1.7.10 while the profile ID is 1.7.10-Forge10.13.4.1614).
        // Match the full suffix when present, otherwise match the numeric build
        // as a complete component so 47.4.2 cannot accidentally match 47.4.28.
        const auto build_suffix = forge_build.find('-');
        const auto build_core = forge_build.substr(0, build_suffix);
        const auto contains_component = [](const std::string& value, const std::string& token) {
            std::size_t position = 0;
            while ((position = value.find(token, position)) != std::string::npos) {
                const auto after = position + token.size();
                if (after == value.size() || value[after] == '-' || value[after] == '_') return true;
                position = after;
            }
            return false;
        };
        if (normalized_profile_id.find("forge") == std::string::npos ||
            (!contains_component(normalized_profile_id, forge_build) &&
             !contains_component(normalized_profile_id, build_core))) continue;
        const auto manifest_path = it->path() / (profile_id + ".json");
        std::ifstream input(manifest_path);
        if (!input) continue;
        auto metadata = json::parse(input, nullptr, false);
        if (!metadata.is_object()) continue;
        const auto id = metadata.value("id", profile_id);
        if (id != profile_id) continue;
        const auto inherited = metadata.value("inheritsFrom", std::string());
        if (!inherited.empty() && inherited != minecraft_version) continue;
        if (inherited.empty() && !profile_id.starts_with(minecraft_version)) continue;
        if (!metadata.contains("mainClass") || !metadata["mainClass"].is_string() ||
            metadata["mainClass"].get<std::string>().empty() ||
            !metadata.contains("libraries") || !metadata["libraries"].is_array()) continue;
        return {profile_id, std::move(metadata)};
    }
    throw std::runtime_error("Forge's installer finished but its generated launcher profile could not be found. See the Forge installer log in the launcher data folder.");
}

class LauncherProfilesStub {
public:
    explicit LauncherProfilesStub(const fs::path& root)
        : path_(root / "launcher_profiles.json"),
          backup_(root / "launcher_profiles.json.obsidian-forge-backup") {
        std::error_code error;
        if (fs::exists(path_, error)) {
            ensure_no_symlink_components(root, "launcher_profiles.json");
            std::ifstream input(path_);
            auto existing = json::parse(input, nullptr, false);
            if (!existing.is_object())
                throw std::runtime_error("Forge requires a valid launcher_profiles.json in the launcher data folder");
            ensure_no_symlink_components(root, "launcher_profiles.json.obsidian-forge-backup");
            fs::copy_file(path_, backup_, fs::copy_options::overwrite_existing, error);
            if (error)
                throw std::runtime_error("Cannot preserve the existing Forge launcher profile file: " + error.message());
            backed_up_ = true;
            return;
        }
        ensure_no_symlink_components(root, "launcher_profiles.json");
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot prepare the Forge installer profile file");
        output << R"({"profiles":{}})";
        if (!output) throw std::runtime_error("Cannot write the Forge installer profile file");
        created_ = true;
    }
    ~LauncherProfilesStub() {
        std::error_code ignored;
        if (backed_up_) {
            fs::remove(path_, ignored);
            ignored.clear();
            fs::rename(backup_, path_, ignored);
            if (ignored) {
                ignored.clear();
                fs::copy_file(backup_, path_, fs::copy_options::overwrite_existing, ignored);
                if (!ignored) fs::remove(backup_, ignored);
            }
        } else if (created_) {
            fs::remove(path_, ignored);
        }
    }
    LauncherProfilesStub(const LauncherProfilesStub&) = delete;
    LauncherProfilesStub& operator=(const LauncherProfilesStub&) = delete;
private:
    fs::path path_;
    fs::path backup_;
    bool created_ = false;
    bool backed_up_ = false;
};

std::string sha1_token(std::string text) {
    const auto first = text.find_first_of("0123456789abcdefABCDEF");
    if (first == std::string::npos || text.size() - first < 40)
        throw std::runtime_error("Forge installer checksum metadata is invalid");
    const auto value = text.substr(first, 40);
    if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch); }))
        throw std::runtime_error("Forge installer checksum metadata is invalid");
    return lower(value);
}

}  // namespace

json install(const std::string& data_dir, const std::string& instance_id,
             const std::shared_ptr<std::atomic_bool>& cancelled, const Progress& progress) {
    std::lock_guard<std::mutex> install_lock(g_install_mutex);
    InstallContext context{cancelled, progress};
    context.check_cancelled();
    if (!safe_identifier(instance_id)) throw std::runtime_error("Invalid instance identifier");
    fs::path profile_path;
    const auto profile = load_instance(data_dir, instance_id, profile_path);
    const auto version_id = profile.value("version", std::string());
    if (!safe_identifier(version_id)) throw std::runtime_error("Invalid Minecraft version identifier");

    const fs::path root(data_dir);
    const fs::path versions_dir = root / "versions" / version_id;
    const fs::path metadata_path = versions_dir / (version_id + ".json");
    const fs::path client_path = versions_dir / (version_id + ".jar");
    const fs::path minecraft_dir = profile_path / ".minecraft";
    ensure_no_symlink_components(root, fs::path("versions") / version_id / (version_id + ".json"));
    ensure_no_symlink_components(root, fs::path("versions") / version_id / (version_id + ".jar"));
    ensure_no_symlink_components(root, fs::path("libraries"));
    ensure_no_symlink_components(root, fs::path("assets"));
    ensure_no_symlink_components(root, fs::path("natives"));
    ensure_no_symlink_components(profile_path, fs::path(".minecraft"));
    for (const auto* folder : {"mods", "saves", "resourcepacks", "shaderpacks", "config", "logs"})
        fs::create_directories(minecraft_dir / folder);
    fs::create_directories(root / "libraries");
    fs::create_directories(root / "assets" / "indexes");
    fs::create_directories(root / "assets" / "objects");
    fs::create_directories(root / "natives");
    fs::create_directories(versions_dir);

    context.report("Preparing Minecraft " + version_id, 1);
    const auto version_entry = launcher::version_manifest::fetch(data_dir).at("versions");
    const json* selected = nullptr;
    for (const auto& item : version_entry) {
        if (item.is_object() && item.value("id", std::string()) == version_id) { selected = &item; break; }
    }
    if (!selected || selected->value("url", std::string()).empty())
        throw std::runtime_error("Minecraft version is missing from the Mojang manifest; refresh the version list and try again");

    json metadata;
    std::string metadata_text;
    const auto metadata_sha1 = selected->value("sha1", std::string());
    if (file_matches(metadata_path, metadata_sha1, 0)) {
        std::ifstream cached(metadata_path);
        metadata_text.assign(std::istreambuf_iterator<char>(cached), std::istreambuf_iterator<char>());
        metadata = json::parse(metadata_text, nullptr, false);
    }
    if (!metadata.is_object()) {
        context.report("Fetching Minecraft " + version_id + " metadata", 4);
        metadata_text = fetch_text(selected->at("url").get<std::string>(), context);
        metadata = json::parse(metadata_text, nullptr, false);
        if (!metadata.is_object()) throw std::runtime_error("Minecraft returned invalid version metadata");
        if (!metadata_sha1.empty() && lower(sha1_string(metadata_text)) != lower(metadata_sha1))
            throw std::runtime_error("Minecraft version metadata failed its SHA-1 check");
        const auto temporary = fs::path(metadata_path.string() + ".tmp");
        { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output << metadata_text; }
        replace_file(temporary, metadata_path);
    }

    const auto selected_loader = profile.value("loader", std::string("vanilla"));
    json loader_profile;
    std::string loader_version;
    std::string loader_profile_id;
    json launch_metadata = metadata;
    const auto fabric_profile_path = minecraft_dir / "obsidian-fabric-profile.json";
    ensure_no_symlink_components(profile_path, fs::path(".minecraft") / "obsidian-fabric-profile.json");
    if (selected_loader == "fabric") {
        context.report("Finding the latest stable Fabric Loader", 7);
        auto fabric = launcher::mod_loader::fetch_latest_fabric_profile(version_id, cancelled);
        loader_version = fabric.loader_version;
        loader_profile = std::move(fabric.profile);
        launch_metadata = launcher::mod_loader::merge_fabric_profile(metadata, loader_profile);
        const auto temporary = fs::path(fabric_profile_path.string() + ".tmp");
        { std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
          output << loader_profile.dump(2);
          if (!output) throw std::runtime_error("Cannot save Fabric launcher profile"); }
        replace_file(temporary, fabric_profile_path);
        context.report("Fabric Loader " + loader_version + " selected", 8);
    } else if (selected_loader == "forge") {
        context.report("Checking Forge builds for Minecraft " + version_id, 7);
        loader_version = launcher::mod_loader::fetch_latest_forge_version(version_id, cancelled);
        const auto forge_artifact_version = version_id + "-" + loader_version;
        const auto installer_name = "forge-" + forge_artifact_version + "-installer.jar";
        const auto installer_path = root / "meta" / installer_name;
        const auto log_path = root / "meta" / ("forge-installer-" + forge_artifact_version + ".log");
        fs::create_directories(root / "meta");
        ensure_no_symlink_components(root, fs::path("meta") / installer_name);
        ensure_no_symlink_components(root, fs::path("meta") / ("forge-installer-" + forge_artifact_version + ".log"));
        const auto installer_url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" +
                                   forge_artifact_version + "/" + installer_name;
        context.report("Downloading Forge " + loader_version + " installer", 9);
        const auto checksum_text = fetch_text(installer_url + ".sha1", context);
        const auto installer_sha1 = sha1_token(checksum_text);
        download_one({installer_url, installer_path, installer_sha1, 0}, context);

        context.report("Running Forge's official installer and client processors", 12);
        {
            LauncherProfilesStub launcher_profiles(root);
            const auto java = installer_java_executable(profile);
            const auto exit_code = run_forge_installer(java, installer_path,
                                                       fs::absolute(root), log_path, context);
            if (exit_code != 0) {
                auto details = read_log_tail(log_path);
                throw std::runtime_error("Forge installer failed with exit code " +
                    std::to_string(exit_code) + (details.empty() ? std::string() : "\n" + details) +
                    "\nFull installer log: " + log_path.string());
            }
            context.check_cancelled();
            auto installed = find_forge_profile(root, version_id, loader_version);
            loader_profile_id = std::move(installed.first);
            loader_profile = std::move(installed.second);
        }
        launch_metadata = launcher::mod_loader::merge_forge_profile(metadata, loader_profile);
        context.report("Forge " + loader_version + " installed", 17);
        std::error_code ignored_remove;
        fs::remove(fabric_profile_path, ignored_remove);
    } else if (selected_loader == "vanilla") {
        std::error_code ignored_remove;
        fs::remove(fabric_profile_path, ignored_remove);
    } else {
        throw std::runtime_error("This mod loader is not supported by the installer yet: " + selected_loader);
    }

    if (!metadata.contains("downloads") || !metadata["downloads"].is_object() ||
        !metadata["downloads"].contains("client"))
        throw std::runtime_error("This Minecraft version does not provide a client download");
    const auto& client = metadata["downloads"]["client"];
    if (!client.is_object() || !client.contains("url"))
        throw std::runtime_error("Minecraft client download metadata is invalid");
    const int client_start = selected_loader == "forge" ? 18 : 8;
    const int client_end = selected_loader == "forge" ? 30 : 22;
    const int library_start = client_end;
    const int library_span = selected_loader == "forge" ? 20 : 23;
    const int assets_start = library_start + library_span + 2;
    const int assets_span = 94 - assets_start;
    context.report("Downloading Minecraft " + version_id + " client", client_start);
    download_one({client.at("url").get<std::string>(), client_path,
                  client.value("sha1", std::string()), client.value("size", std::uint64_t{0})}, context);
    context.report("Minecraft client installed", client_end);

    std::vector<NativeArchive> natives;
    const auto libraries = prepare_libraries(launch_metadata, root, natives,
                                             selected_loader == "forge");
    download_group(libraries, context, "libraries", library_start, library_span);

    json asset_index = metadata.value("assetIndex", json::object());
    if (!asset_index.is_object() || !asset_index.contains("url")) {
        const auto legacy_id = metadata.value("assets", version_id);
        if (!safe_identifier(legacy_id)) throw std::runtime_error("Minecraft asset index identifier is invalid");
        asset_index = {{"id", legacy_id},
                       {"url", "https://s3.amazonaws.com/Minecraft.Download/indexes/" + legacy_id + ".json"}};
    }
    const auto asset_index_id = asset_index.value("id", version_id);
    if (!safe_identifier(asset_index_id)) throw std::runtime_error("Minecraft asset index identifier is invalid");
    const auto asset_index_path = root / "assets" / "indexes" / (asset_index_id + ".json");
    const auto asset_index_sha1 = asset_index.value("sha1", std::string());
    ensure_no_symlink_components(root, fs::path("assets") / "indexes" / (asset_index_id + ".json"));
    if (!file_matches(asset_index_path, asset_index_sha1, asset_index.value("size", std::uint64_t{0}))) {
        context.report("Fetching Minecraft asset index", assets_start);
        const auto text = fetch_text(asset_index.at("url").get<std::string>(), context);
        const auto index_json = json::parse(text, nullptr, false);
        if (!index_json.is_object()) throw std::runtime_error("Minecraft returned an invalid asset index");
        if (!asset_index_sha1.empty() && lower(sha1_string(text)) != lower(asset_index_sha1))
            throw std::runtime_error("Minecraft asset index failed its SHA-1 check");
        const auto temporary = fs::path(asset_index_path.string() + ".tmp");
        { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output << text; }
        replace_file(temporary, asset_index_path);
    }
    std::ifstream index_file(asset_index_path);
    auto index_json = json::parse(index_file, nullptr, false);
    if (!index_json.is_object()) throw std::runtime_error("Minecraft asset index could not be read");
    const auto assets = prepare_assets(index_json, root);
    download_group(assets, context, "assets", assets_start, assets_span);

    const auto native_dir = root / "natives" / version_id;
    std::error_code ignored;
    fs::remove_all(native_dir, ignored);
    fs::create_directories(native_dir);
    for (const auto& native : natives) {
        context.check_cancelled();
        extract_native_archive(native, native_dir, context);
    }
    context.report("Setting up this instance", 97);
    context.check_cancelled();
    auto installed_profile = profile;
    installed_profile["installed"] = true;
    installed_profile["installedVersion"] = version_id;
    installed_profile["installedLoader"] = selected_loader;
    if (selected_loader == "fabric" || selected_loader == "forge") {
        installed_profile["installedLoaderVersion"] = loader_version;
    } else {
        installed_profile.erase("installedLoaderVersion");
    }
    if (selected_loader == "forge") {
        installed_profile["installedLoaderProfileId"] = loader_profile_id;
    } else {
        installed_profile.erase("installedLoaderProfileId");
    }
    launcher::instance::save(data_dir, installed_profile);
    context.report("Minecraft " + version_id + " installed successfully", 100, "complete");
    return {{"instanceId", instance_id}, {"version", version_id}, {"installed", true},
            {"loader", selected_loader}, {"loaderVersion", loader_version},
            {"loaderProfileId", loader_profile_id},
            {"libraryCount", libraries.size()}, {"assetCount", assets.size()},
            {"nativeCount", natives.size()}};
}

}  // namespace launcher::minecraft_installer
