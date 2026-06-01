#include "controllers/auth_controller.h"
#include <nlohmann/json.hpp>

namespace kiftd {

void register_auth_routes(crow::SimpleApp& app, Database& db, Auth& auth) {

    // POST /api/auth/login
    CROW_ROUTE(app, "/api/auth/login")
        .methods("POST"_method)
    ([&db, &auth](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (!auth.login(username, password)) {
            return crow::response(401, R"({"error":"invalid credentials"})");
        }

        crow::response res(200, R"({"ok":true})");
        // Simple cookie-based session
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
