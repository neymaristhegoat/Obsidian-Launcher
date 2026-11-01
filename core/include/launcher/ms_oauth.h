#pragma once
#include <functional>
#include <string>

namespace launcher::oauth {

struct DeviceCode {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    int expires_in = 900;
    int interval = 5;
};

struct MsTokens {
    std::string access_token;
    std::string refresh_token;
};

DeviceCode request_device_code(const std::string& client_id);

// Blocks until the user finishes signing in. Throws on denied/expired/cancelled.
MsTokens poll_device_token(const std::string& client_id, const DeviceCode& dc,
                           const std::function<bool()>& cancelled);

MsTokens refresh_ms_token(const std::string& client_id, const std::string& refresh_token);

// Refresh token storage (a file for now; move to the OS keychain later)
void save_refresh_token(const std::string& dir, const std::string& token);
std::string load_refresh_token(const std::string& dir);
void clear_refresh_token(const std::string& dir);

}