#include "launcher/auth.h"
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

using json = nlohmann::json;

namespace launcher::auth {

namespace {

json post_json(const std::string& url, const json& body, bool allow_401 = false) {
    auto r = cpr::Post(
        cpr::Url{url},
        cpr::Header{{"Content-Type", "application/json"}, {"Accept", "application/json"}},
        cpr::Body{body.dump()},
        cpr::Timeout{15000});

    if (r.error) throw std::runtime_error("Network error: " + r.error.message);
    if (r.status_code == 401 && allow_401) return json::parse(r.text, nullptr, false);
    if (r.status_code != 200)
        throw std::runtime_error("POST " + url + " failed (" +
                                 std::to_string(r.status_code) + "): " + r.text);
    json j = json::parse(r.text, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error("Invalid JSON from " + url);
    return j;
}

std::string xsts_error_message(long long xerr) {
    switch (xerr) {
        case 2148916233: return "This Microsoft account has no Xbox account. Sign in at xbox.com once to create one.";
        case 2148916235: return "Xbox Live is not available in this account's country/region.";
        case 2148916236:
        case 2148916237: return "This account needs adult verification on the Xbox site.";
        case 2148916238: return "This is a child account. An adult must add it to a Microsoft family.";
        default:         return "Xbox authorization failed (XErr " + std::to_string(xerr) + ").";
    }
}

} // namespace

// Step 3
XboxToken xbl_authenticate(const std::string& ms_access_token) {
    json body = {
        {"Properties", {
            {"AuthMethod", "RPS"},
            {"SiteName", "user.auth.xboxlive.com"},
            {"RpsTicket", "d=" + ms_access_token}
        }},
        {"RelyingParty", "http://auth.xboxlive.com"},
        {"TokenType", "JWT"}
    };
    json j = post_json("https://user.auth.xboxlive.com/user/authenticate", body);
    return { j.at("Token").get<std::string>(),
             j.at("DisplayClaims").at("xui").at(0).at("uhs").get<std::string>(),
             j.at("DisplayClaims").at("xui").at(0).value("xid", std::string()) };
}

// Step 4
XboxToken xsts_authorize(const std::string& xbl_token) {
    json body = {
        {"Properties", {
            {"SandboxId", "RETAIL"},
            {"UserTokens", json::array({xbl_token})}
        }},
        {"RelyingParty", "rp://api.minecraftservices.com/"},
        {"TokenType", "JWT"}
    };
    json j = post_json("https://xsts.auth.xboxlive.com/xsts/authorize", body, /*allow_401=*/true);

    if (j.is_object() && j.contains("XErr"))
        throw std::runtime_error(xsts_error_message(j["XErr"].get<long long>()));

    return { j.at("Token").get<std::string>(),
             j.at("DisplayClaims").at("xui").at(0).at("uhs").get<std::string>(),
             j.at("DisplayClaims").at("xui").at(0).value("xid", std::string()) };
}

// Step 5
std::string minecraft_login(const XboxToken& xsts) {
    json body = {{"identityToken", "XBL3.0 x=" + xsts.uhs + ";" + xsts.token}};
    json j = post_json("https://api.minecraftservices.com/authentication/login_with_xbox", body);
    return j.at("access_token").get<std::string>();
}

// Step 6
bool owns_game(const std::string& mc_token) {
    auto r = cpr::Get(cpr::Url{"https://api.minecraftservices.com/entitlements/mcstore"},
                      cpr::Bearer{mc_token}, cpr::Timeout{15000});
    if (r.status_code != 200)
        throw std::runtime_error("Entitlement check failed (" + std::to_string(r.status_code) + ")");
    json j = json::parse(r.text, nullptr, false);
    return j.is_object() && j.contains("items") && !j["items"].empty();
}

// Step 7
McSession get_profile(const std::string& mc_token, const std::string& xuid) {
    auto r = cpr::Get(cpr::Url{"https://api.minecraftservices.com/minecraft/profile"},
                      cpr::Bearer{mc_token}, cpr::Timeout{15000});
    if (r.status_code == 404)
        throw std::runtime_error("This account doesn't own Minecraft: Java Edition.");
    if (r.status_code != 200)
        throw std::runtime_error("Profile request failed (" + std::to_string(r.status_code) + ")");
    json j = json::parse(r.text);
    return { mc_token, j.at("id").get<std::string>(), j.at("name").get<std::string>(), xuid };
}

McSession login_with_ms_token(const std::string& ms_access_token) {
    spdlog::info("auth: Xbox Live");
    auto xbl = xbl_authenticate(ms_access_token);
    spdlog::info("auth: XSTS");
    auto xsts = xsts_authorize(xbl.token);
    spdlog::info("auth: Minecraft token");
    auto mc = minecraft_login(xsts);
    spdlog::info("auth: profile");
    return get_profile(mc, xsts.xuid);   // 404 here means no Java Edition
}

} // namespace launcher::auth
