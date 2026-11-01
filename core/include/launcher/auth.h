#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace launcher::auth {

struct McSession {
    std::string access_token;   // Minecraft token, passed to the game
    std::string uuid;           // --uuid
    std::string username;       // --username
};

struct XboxToken {
    std::string token;
    std::string uhs;            // user hash
};

XboxToken xbl_authenticate(const std::string& ms_access_token);
XboxToken xsts_authorize(const std::string& xbl_token);
std::string minecraft_login(const XboxToken& xsts);
bool owns_game(const std::string& mc_token);
McSession get_profile(const std::string& mc_token);

// Runs steps 3-7 in order. Throws std::runtime_error with a readable message.
McSession login_with_ms_token(const std::string& ms_access_token);

}