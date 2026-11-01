#include "launcher/accounts.h"
#include "launcher/auth.h"
#include "launcher/ms_oauth.h"
#include "launcher/offline.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

using json = nlohmann::json;
using Handler = std::function<json(const json& params)>;
namespace acc = launcher::accounts;

namespace {

// ---------- output (stdout is protocol only) ----------

std::mutex g_out_mutex;

void send(const json& msg) {
    std::lock_guard<std::mutex> lock(g_out_mutex);
    std::cout << msg.dump() << '\n' << std::flush;
}

void notify(const std::string& method, const json& params) {
    send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

void reply_result(const json& id, const json& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void reply_error(const json& id, int code, const std::string& message) {
    send({{"jsonrpc", "2.0"},
          {"id", id},
          {"error", {{"code", code}, {"message", message}}}});
}

// ---------- method registry ----------

struct Method {
    Handler fn;
    bool async;
};

std::unordered_map<std::string, Method> g_methods;

void add_sync(const std::string& name, Handler fn)  { g_methods[name] = {std::move(fn), false}; }
void add_async(const std::string& name, Handler fn) { g_methods[name] = {std::move(fn), true};  }

// ---------- in-flight request tracking ----------

std::mutex g_inflight_mutex;
std::condition_variable g_inflight_cv;
int g_inflight = 0;

void run_request(const Method& m, const json& id, const json& params, bool wants_reply) {
    try {
        json result = m.fn(params);
        if (wants_reply) reply_result(id, result);
    } catch (const json::exception& e) {
        if (wants_reply) reply_error(id, -32602, std::string("Invalid params: ") + e.what());
    } catch (const std::exception& e) {
        spdlog::error("handler error: {}", e.what());
        if (wants_reply) reply_error(id, -32000, e.what());
    }
}

void dispatch(const json& req) {
    if (!req.is_object() || !req.contains("method") || !req["method"].is_string()) {
        reply_error(nullptr, -32600, "Invalid request");
        return;
    }

    const bool wants_reply = req.contains("id");
    const json id = wants_reply ? req["id"] : json(nullptr);
    const std::string name = req["method"];
    const json params = req.value("params", json::object());

    auto it = g_methods.find(name);
    if (it == g_methods.end()) {
        if (wants_reply) reply_error(id, -32601, "Method not found: " + name);
        return;
    }

    const Method& m = it->second;
    if (!m.async) {
        run_request(m, id, params, wants_reply);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_inflight_mutex);
        ++g_inflight;
    }
    std::thread([&m, id, params, wants_reply] {
        run_request(m, id, params, wants_reply);
        {
            std::lock_guard<std::mutex> lock(g_inflight_mutex);
            --g_inflight;
        }
        g_inflight_cv.notify_all();
    }).detach();
}

// ---------- auth state ----------

std::string g_data_dir = ".";
std::mutex g_auth_mutex;
std::thread g_auth_thread;
std::atomic<bool> g_auth_cancel{false};

// call with g_auth_mutex held
void stop_auth_thread() {
    g_auth_cancel = true;
    if (g_auth_thread.joinable()) g_auth_thread.join();
    g_auth_cancel = false;
}

// What the UI sees for a saved account (never includes the refresh token)
json account_public(const acc::Account& a) {
    return {{"id", a.id}, {"type", a.type}, {"username", a.username}, {"uuid", a.uuid}};
}

json offline_session(const acc::Account& a) {
    return {{"id", a.id}, {"type", "offline"}, {"username", a.username},
            {"uuid", a.uuid}, {"accessToken", "0"}, {"offline", true}};
}

json microsoft_session(const launcher::auth::McSession& s) {
    return {{"id", acc::make_id("microsoft", s.uuid)}, {"type", "microsoft"},
            {"username", s.username}, {"uuid", s.uuid},
            {"accessToken", s.access_token}, {"offline", false}};
}

// Refreshes a saved Microsoft account and returns a live session.
// Refresh tokens rotate, so the new one is saved BEFORE the Minecraft steps.
json refresh_account(const acc::Account& a, const std::string& client_id) {
    if (a.refresh_token.empty()) throw std::runtime_error("No saved login for this account");
    auto t = launcher::oauth::refresh_ms_token(client_id, a.refresh_token);
    if (!t.refresh_token.empty()) {
        acc::Account updated = a;
        updated.refresh_token = t.refresh_token;
        acc::upsert(updated, true);
    }
    return microsoft_session(launcher::auth::login_with_ms_token(t.access_token));
}

// ---------- register all methods here ----------

void register_methods() {
    add_sync("core.ping", [](const json&) {
        return json{{"pong", true}};
    });

    // placeholder; replace with the real Mojang manifest fetch later
    add_sync("versions.list", [](const json&) {
        return json::array({"1.21.1", "1.20.4"});
    });

    // demo of a long task that emits notifications
    add_async("core.slowTest", [](const json&) {
        for (int i = 1; i <= 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            notify("download.progress", {{"done", i * 20}, {"total", 100}});
        }
        return json{{"ok", true}};
    });

    // ----- saved accounts -----

    add_sync("accounts.list", [](const json&) {
        json arr = json::array();
        for (const auto& a : acc::list()) arr.push_back(account_public(a));
        return json{{"active", acc::active_id()}, {"accounts", arr}};
    });

    // Makes an account active and returns a live session for it
    add_async("accounts.select", [](const json& p) {
        const std::string id = p.at("id").get<std::string>();
        auto a = acc::get(id);
        if (!a) throw std::runtime_error("Unknown account");
        acc::set_active(id);
        if (a->type == "offline") return offline_session(*a);
        return refresh_account(*a, p.at("clientId").get<std::string>());
    });

    // Sign out: nobody active, but the account stays saved
    add_sync("accounts.signout", [](const json&) {
        acc::set_active("");
        return json{{"ok", true}};
    });

    // Forget an account completely
    add_sync("accounts.remove", [](const json& p) {
        acc::remove(p.at("id").get<std::string>());
        return json{{"ok", true}};
    });

    // ----- new logins -----

    add_sync("auth.offline", [](const json& p) {
        const std::string name = p.at("username").get<std::string>();
        if (!launcher::offline::valid_username(name))
            throw std::runtime_error(
                "Username must be 3-16 characters: letters, numbers, underscore");

        acc::Account a;
        a.type     = "offline";
        a.username = name;
        a.uuid     = launcher::offline::offline_uuid(name);
        a.id       = acc::make_id(a.type, a.uuid);
        acc::upsert(a, true);
        return offline_session(a);
    });

    // Steps 3-7 of the Microsoft chain, given a Microsoft access token (testing aid)
    add_async("auth.mcLogin", [](const json& p) {
        return microsoft_session(
            launcher::auth::login_with_ms_token(p.at("msAccessToken").get<std::string>()));
    });

    // Device code login: returns the code now, finishes via notification
    add_async("auth.start", [](const json& p) {
        const std::string client_id = p.at("clientId").get<std::string>();
        auto dc = launcher::oauth::request_device_code(client_id);

        std::lock_guard<std::mutex> lock(g_auth_mutex);
        stop_auth_thread();
        g_auth_thread = std::thread([client_id, dc] {
            try {
                auto t = launcher::oauth::poll_device_token(
                    client_id, dc, [] { return g_auth_cancel.load(); });
                auto s = launcher::auth::login_with_ms_token(t.access_token);

                acc::Account a;
                a.type          = "microsoft";
                a.username      = s.username;
                a.uuid          = s.uuid;
                a.id            = acc::make_id(a.type, a.uuid);
                a.refresh_token = t.refresh_token;
                acc::upsert(a, true);

                notify("auth.complete", microsoft_session(s));
            } catch (const std::exception& e) {
                if (!g_auth_cancel) notify("auth.failed", {{"message", e.what()}});
            }
        });

        return json{{"userCode", dc.user_code},
                    {"verificationUri", dc.verification_uri},
                    {"expiresIn", dc.expires_in}};
    });

    add_async("auth.cancel", [](const json&) {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        stop_auth_thread();
        return json{{"ok", true}};
    });
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--data-dir") g_data_dir = argv[i + 1];

    // all logging goes to stderr; stdout is reserved for JSON-RPC
    auto logger = spdlog::stderr_color_mt("core");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S] [%l] %v");

    std::ios::sync_with_stdio(false);

    acc::init(g_data_dir);
    register_methods();
    spdlog::info("core ready (data dir: {})", g_data_dir);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        json req = json::parse(line, nullptr, false);
        if (req.is_discarded()) {
            reply_error(nullptr, -32700, "Parse error");
            continue;
        }
        dispatch(req);
    }

    // stdin closed (UI exited): stop sign-in polling, let other work finish, quit
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        stop_auth_thread();
    }
    std::unique_lock<std::mutex> lock(g_inflight_mutex);
    g_inflight_cv.wait(lock, [] { return g_inflight == 0; });
    return 0;
}