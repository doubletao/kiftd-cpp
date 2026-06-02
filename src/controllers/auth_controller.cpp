#include "controllers/auth_controller.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <chrono>

namespace kiftd {

static constexpr int MAX_RECORDS = 10000;

struct FailRecord {
    int count = 0;
    std::chrono::steady_clock::time_point lock_until;
};

static void cleanup_fail_map(std::unordered_map<std::string, FailRecord>& m) {
    auto now = std::chrono::steady_clock::now();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.lock_until < now && it->second.count > 0) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
}

void register_auth_routes(crow::SimpleApp& app, Database& db, Auth& auth, const Config& cfg) {

    static std::unordered_map<std::string, FailRecord> fail_map;

    // POST /api/auth/login
    CROW_ROUTE(app, "/api/auth/login")
        .methods("POST"_method)
    ([&db, &auth, &cfg](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        // Memory safety: lazy cleanup + size cap
        if (fail_map.size() > MAX_RECORDS) {
            cleanup_fail_map(fail_map);
            if (fail_map.size() > MAX_RECORDS) {
                fail_map.clear();
            }
        }

        // Rate limit check
        std::string ip = req.remote_ip_address;
        auto now = std::chrono::steady_clock::now();
        auto& rec = fail_map[ip];
        if (rec.count >= cfg.max_attempts && rec.lock_until > now) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(rec.lock_until - now).count();
            nlohmann::json j;
            j["error"] = "too many attempts";
            j["retry_after"] = remaining;
            return crow::response(429, j.dump());
        }

        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (!auth.login(username, password)) {
            // Reset if lockout expired
            if (rec.lock_until < now) {
                rec.count = 0;
            }
            rec.count++;
            if (rec.count >= cfg.max_attempts) {
                rec.lock_until = now + std::chrono::seconds(cfg.lockout_seconds);
            }
            return crow::response(401, R"({"error":"invalid credentials"})");
        }

        // Success: clear fail record
        fail_map.erase(ip);

        crow::response res(200, R"({"ok":true})");
        res.add_header("Set-Cookie", "kiftd_user=" + username + "; Path=/; HttpOnly; SameSite=Lax");
        return res;
    });

    // POST /api/auth/logout
    CROW_ROUTE(app, "/api/auth/logout")
        .methods("POST"_method)
    ([](const crow::request& req) {
        crow::response res(200, R"({"ok":true})");
        res.add_header("Set-Cookie", "kiftd_user=; Path=/; HttpOnly; Max-Age=0");
        return res;
    });

    // GET /api/auth/me
    CROW_ROUTE(app, "/api/auth/me")
        .methods("GET"_method)
   ([&db](const crow::request& req) {
        auto cookie = req.get_header_value("Cookie");
        std::string user;
        auto pos = cookie.find("kiftd_user=");
        if (pos != std::string::npos) {
            auto start = pos + 11;
            auto end = cookie.find(';', start);
            user = cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        if (user.empty()) {
            return crow::response(401, R"({"error":"not logged in"})");
        }
        nlohmann::json j;
        j["username"] = user;
        return crow::response(200, j.dump());
    });
}

} // namespace kiftd
