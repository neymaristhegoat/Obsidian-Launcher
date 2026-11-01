#include "launcher/auth.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

using json = nlohmann::json;
using Handler = std::function<json(const json& params)>;

namespace {

// ---------- output (stdout is protocol only) ----------

std::mutex g_out_mutex;

void send(const json& msg) {
    std::lock_guard<std::mutex> lock(g_out_mutex);  // many threads may send
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
    bool async;  // true = run on its own thread (network, disk, long work)
};

std::unordered_map<std::string, Method> g_methods;

void add_sync(const std::string& name, Handler fn)  { g_methods[name] = {std::move(fn), false}; }
void add_async(const std::string& name, Handler fn) { g_methods[name] = {std::move(fn), true};  }

// ---------- in-flight request tracking (so we exit cleanly) ----------

std::mutex g_inflight_mutex;
std::condition_variable g_inflight_cv;
int g_inflight = 0;

void run_request(const Method& m, const json& id, const json& params, bool wants_reply) {
    try {
        json result = m.fn(params);
        if (wants_reply) reply_result(id, result);
    } catch (const json::exception& e) {
        // missing/invalid params from .at() / .get<>()
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

    const bool wants_reply = req.contains("id");           // no id = client notification
    const json id = wants_reply ? req["id"] : json(nullptr);
    const std::string name = req["method"];
    const json params = req.value("params", json::object());

    auto it = g_methods.find(name);
    if (it == g_methods.end()) {
        if (wants_reply) reply_error(id, -32601, "Method not found: " + name);
        return;
    }

    const Method& m = it->second;  // map is never modified after startup, so this is safe
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

// ---------- register all methods here ----------

void register_methods() {
    add_sync("core.ping", [](const json&) {
        return json{{"pong", true}};
    });

    // placeholder; replace with the real Mojang manifest fetch later
    add_sync("versions.list", [](const json&) {
        return json::array({"1.21.1", "1.20.4"});
    });

    // Steps 3-7 of the Microsoft chain, given a Microsoft access token
    add_async("auth.mcLogin", [](const json& p) {
        auto s = launcher::auth::login_with_ms_token(p.at("msAccessToken").get<std::string>());
        return json{{"uuid", s.uuid}, {"username", s.username}, {"accessToken", s.access_token}};
    });

    // demo of a long task that emits notifications (delete once you have real ones)
    add_async("core.slowTest", [](const json&) {
        for (int i = 1; i <= 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            notify("download.progress", {{"done", i * 20}, {"total", 100}});
        }
        return json{{"ok", true}};
    });
}

}  // namespace

int main() {
    // all logging goes to stderr; stdout is reserved for JSON-RPC
    auto logger = spdlog::stderr_color_mt("core");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S] [%l] %v");

    std::ios::sync_with_stdio(false);

    register_methods();
    spdlog::info("core ready");

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

    // stdin closed (UI exited): let running async work finish, then quit
    std::unique_lock<std::mutex> lock(g_inflight_mutex);
    g_inflight_cv.wait(lock, [] { return g_inflight == 0; });
    return 0;
}