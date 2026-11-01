#include "launcher/accounts.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace launcher::accounts {

namespace {

std::mutex g_mu;
std::string g_dir = ".";
std::vector<Account> g_accounts;
std::string g_active;

fs::path file_path() { return fs::path(g_dir) / "accounts.json"; }

// call with g_mu held. Writes to a temp file, then renames, so a crash can't corrupt it.
void save_locked() {
    json j;
    j["active"] = g_active;
    j["accounts"] = json::array();
    for (const auto& a : g_accounts)
        j["accounts"].push_back({{"id", a.id},
                                 {"type", a.type},
                                 {"username", a.username},
                                 {"uuid", a.uuid},
                                 {"refreshToken", a.refresh_token}});

    fs::create_directories(g_dir);
    fs::path tmp = file_path();
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot write " + tmp.string());
        f << j.dump(2);
    }
    std::error_code ec;   // owner-only on Linux/macOS; mostly a no-op on Windows
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    fs::rename(tmp, file_path());
}

}  // namespace

void init(const std::string& data_dir) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_dir = data_dir;
    g_accounts.clear();
    g_active.clear();

    std::ifstream f(file_path());
    if (!f) return;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object() || !j.contains("accounts") || !j["accounts"].is_array()) return;

    for (const auto& e : j["accounts"]) {
        if (!e.is_object()) continue;
        Account a;
        a.id            = e.value("id", std::string());
        a.type          = e.value("type", std::string());
        a.username      = e.value("username", std::string());
        a.uuid          = e.value("uuid", std::string());
        a.refresh_token = e.value("refreshToken", std::string());
        if (!a.id.empty() && !a.username.empty()) g_accounts.push_back(std::move(a));
    }
    g_active = j.value("active", std::string());
}

std::vector<Account> list() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_accounts;
}

std::string active_id() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_active;
}

std::optional<Account> get(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (const auto& a : g_accounts)
        if (a.id == id) return a;
    return std::nullopt;
}

void upsert(const Account& a, bool make_active) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = std::find_if(g_accounts.begin(), g_accounts.end(),
                           [&](const Account& x) { return x.id == a.id; });
    if (it != g_accounts.end()) *it = a;
    else g_accounts.push_back(a);
    if (make_active) g_active = a.id;
    save_locked();
}

void set_active(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_active = id;
    save_locked();
}

void remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_accounts.erase(std::remove_if(g_accounts.begin(), g_accounts.end(),
                                    [&](const Account& x) { return x.id == id; }),
                     g_accounts.end());
    if (g_active == id) g_active.clear();
    save_locked();
}

}  // namespace launcher::accounts