#include "launcher/accounts.h"

#include <algorithm>
#include <mutex>

using json = nlohmann::json;

namespace launcher::accounts {
namespace {

std::mutex g_mu;
std::vector<Account> g_accounts;
std::string g_active;

json account_json(const Account& account) {
    return {{"id", account.id}, {"type", account.type}, {"username", account.username},
            {"uuid", account.uuid}, {"refreshToken", account.refresh_token}};
}

}  // namespace

void init(const std::string&) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_accounts.clear();
    g_active.clear();
}

void restore(const json& snapshot_value) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_accounts.clear();
    g_active.clear();
    if (!snapshot_value.is_object() || !snapshot_value.contains("accounts") ||
        !snapshot_value["accounts"].is_array()) return;

    for (const auto& value : snapshot_value["accounts"]) {
        if (!value.is_object()) continue;
        Account account;
        account.id = value.value("id", std::string());
        account.type = value.value("type", std::string());
        account.username = value.value("username", std::string());
        account.uuid = value.value("uuid", std::string());
        account.refresh_token = value.value("refreshToken", std::string());
        if (!account.id.empty() && !account.username.empty())
            g_accounts.push_back(std::move(account));
    }
    g_active = snapshot_value.value("active", std::string());
}

json snapshot() {
    std::lock_guard<std::mutex> lock(g_mu);
    json result{{"active", g_active}, {"accounts", json::array()}};
    for (const auto& account : g_accounts) result["accounts"].push_back(account_json(account));
    return result;
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
    for (const auto& account : g_accounts)
        if (account.id == id) return account;
    return std::nullopt;
}

void upsert(const Account& account, bool make_active) {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto found = std::find_if(g_accounts.begin(), g_accounts.end(),
                                    [&](const Account& existing) { return existing.id == account.id; });
    if (found != g_accounts.end()) *found = account;
    else g_accounts.push_back(account);
    if (make_active) g_active = account.id;
}

void set_active(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_active = id;
}

void remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_accounts.erase(std::remove_if(g_accounts.begin(), g_accounts.end(),
                                    [&](const Account& account) { return account.id == id; }),
                     g_accounts.end());
    if (g_active == id) g_active.clear();
}

}  // namespace launcher::accounts
