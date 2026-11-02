#include "launcher/ms_oauth.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;

namespace launcher::oauth {

namespace {

const std::string kBase  = "https://login.microsoftonline.com/consumers/oauth2/v2.0";
const std::string kScope = "XboxLive.signin offline_access";

json form_post(const std::string& url, const cpr::Payload& payload, long& status) {
    auto r = cpr::Post(cpr::Url{url}, payload, cpr::Timeout{15000});
    if (r.error) throw std::runtime_error("Network error: " + r.error.message);
    status = r.status_code;
    json j = json::parse(r.text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        throw std::runtime_error("Unexpected response from " + url + " (" +
                                 std::to_string(r.status_code) + ")");
    return j;
}

MsTokens parse_tokens(const json& j) {
    return { j.at("access_token").get<std::string>(),
             j.value("refresh_token", std::string()) };
}

} // namespace

DeviceCode request_device_code(const std::string& client_id) {
    long status = 0;
    json j = form_post(kBase + "/devicecode",
                       cpr::Payload{{"client_id", client_id}, {"scope", kScope}}, status);
    if (status != 200)
        throw std::runtime_error("Device code request failed: " +
                                 j.value("error_description", j.dump()));
    DeviceCode dc;
    dc.device_code      = j.at("device_code").get<std::string>();
    dc.user_code        = j.at("user_code").get<std::string>();
    dc.verification_uri = j.at("verification_uri").get<std::string>();
    dc.expires_in       = j.value("expires_in", 900);
    dc.interval         = j.value("interval", 5);
    return dc;
}

MsTokens poll_device_token(const std::string& client_id, const DeviceCode& dc,
                           const std::function<bool()>& cancelled) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(dc.expires_in);
    int interval = dc.interval;

    while (clock::now() < deadline) {
        // sleep in small steps so cancelling is responsive
        for (int i = 0; i < interval * 5; ++i) {
            if (cancelled()) throw std::runtime_error("Sign-in cancelled");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        long status = 0;
        json j = form_post(kBase + "/token",
            cpr::Payload{{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                         {"client_id", client_id},
                         {"device_code", dc.device_code}}, status);

        if (status == 200) return parse_tokens(j);

        const std::string err = j.value("error", std::string());
        if (err == "authorization_pending") continue;
        if (err == "slow_down") { interval += 5; continue; }
        throw std::runtime_error(j.value("error_description", err));
    }
    throw std::runtime_error("The sign-in code expired. Please start again.");
}

MsTokens refresh_ms_token(const std::string& client_id, const std::string& refresh_token) {
    long status = 0;
    json j = form_post(kBase + "/token",
        cpr::Payload{{"grant_type", "refresh_token"},
                     {"client_id", client_id},
                     {"refresh_token", refresh_token},
                     {"scope", kScope}}, status);
    if (status != 200)
        throw std::runtime_error("Session expired, please sign in again: " +
                                 j.value("error_description", j.value("error", std::string())));
    return parse_tokens(j);   // refresh tokens rotate, so save the new one
}

} // namespace launcher::oauth
