#pragma once
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace launcher::accounts {

struct Account {
    std::string id;             // "<type>:<uuid>"
    std::string type;           // "offline" or "microsoft"
    std::string username;
    std::string uuid;
    std::string refresh_token;  // microsoft only, never sent to the UI
};

inline std::string make_id(const std::string& type, const std::string& uuid) {
    return type + ":" + uuid;
}

void init(const std::string& data_dir);               // Credentials are persisted by Electron safeStorage.
void restore(const nlohmann::json& snapshot);
nlohmann::json snapshot();                            // Internal main-process persistence only.
std::vector<Account> list();
std::string active_id();
std::optional<Account> get(const std::string& id);
void upsert(const Account& a, bool make_active);
void set_active(const std::string& id);               // "" = nobody signed in
void remove(const std::string& id);

}
