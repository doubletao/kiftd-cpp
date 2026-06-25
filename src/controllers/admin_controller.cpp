#include "controllers/admin_controller.h"
#include "utils/sha256.h"
#include <nlohmann/json.hpp>

namespace kiftd {

static std::string get_user(const crow::request& req) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("kiftd_user=");
    if (pos == std::string::npos) return "";
    auto start = pos + 11;
    auto end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void register_admin_routes(crow::SimpleApp& app, Database& db, Auth& auth) {

    // GET /api/users - list all users (admin only)
    CROW_ROUTE(app, "/api/users")
        .methods("GET"_method)
    ([&db, &auth](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");
        if (!auth.is_admin(user)) return crow::response(403, R"({"error":"admin access required"})");

        auto users = db.get_all_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& u : users) {
            nlohmann::json j;
            j["username"] = u.id;
            j["role"] = u.role;
            j["created_at"] = u.created_at;
            arr.push_back(j);
        }
        return crow::response(200, arr.dump());
    });

    // POST /api/users - create user (admin only)
    CROW_ROUTE(app, "/api/users")
        .methods("POST"_method)
    ([&db, &auth](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");
        if (!auth.is_admin(user)) return crow::response(403, R"({"error":"admin access required"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
            return crow::response(400, R"({"error":"username and password required"})");
        }

        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (username.empty() || password.empty()) {
            return crow::response(400, R"({"error":"username and password required"})");
        }

        if (db.user_exists(username)) {
            return crow::response(409, R"({"error":"username already exists"})");
        }

        // password is already client-hashed
        std::string salt = generate_salt();
        std::string hash = Auth::hash_password(password, salt);
        if (db.create_user(username, hash, salt, "user")) {
            nlohmann::json j;
            j["message"] = "user created";
            j["username"] = username;
            return crow::response(201, j.dump());
        }
        return crow::response(500, R"({"error":"failed to create user"})");
    });

    // DELETE /api/users/:id - delete user (admin only)
    CROW_ROUTE(app, "/api/users/<string>")
        .methods("DELETE"_method)
    ([&db, &auth](const crow::request& req, const std::string& username) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");
        if (!auth.is_admin(user)) return crow::response(403, R"({"error":"admin access required"})");

        if (username == user) {
            return crow::response(400, R"({"error":"cannot delete yourself"})");
        }

        if (!db.user_exists(username)) {
            return crow::response(404, R"({"error":"user not found"})");
        }

        if (auth.is_admin(username)) {
            return crow::response(400, R"({"error":"cannot delete admin user"})");
        }

        if (db.delete_user(username)) {
            return crow::response(200, R"({"ok":true})");
        }
        return crow::response(500, R"({"error":"failed to delete user"})");
    });

    // PUT /api/users/:id/password - reset password (admin only)
    CROW_ROUTE(app, "/api/users/<string>/password")
        .methods("PUT"_method)
    ([&db, &auth](const crow::request& req, const std::string& username) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");
        if (!auth.is_admin(user)) return crow::response(403, R"({"error":"admin access required"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("password")) {
            return crow::response(400, R"({"error":"password required"})");
        }

        std::string password = body["password"].get<std::string>();
        if (password.empty()) {
            return crow::response(400, R"({"error":"password required"})");
        }

        if (!db.user_exists(username)) {
            return crow::response(404, R"({"error":"user not found"})");
        }

        if (auth.is_admin(username)) {
            return crow::response(400, R"({"error":"cannot reset admin password"})");
        }

        // password is already client-hashed
        std::string salt = generate_salt();
        std::string hash = Auth::hash_password(password, salt);
        if (db.update_password(username, hash, salt)) {
            return crow::response(200, R"({"ok":true})");
        }
        return crow::response(500, R"({"error":"failed to reset password"})");
    });
}

} // namespace kiftd
