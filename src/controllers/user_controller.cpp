#include "controllers/user_controller.h"
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

void register_user_routes(crow::SimpleApp& app, Database& db, Auth& auth) {

    // PUT /api/users/me/password - change own password (user only, not admin)
    CROW_ROUTE(app, "/api/users/me/password")
        .methods("PUT"_method)
    ([&db, &auth](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Admin password is managed by config file
        if (auth.is_admin(user)) {
            return crow::response(403, R"({"error":"admin password is managed by config file"})");
        }

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("old_password") || !body.contains("new_password")) {
            return crow::response(400, R"({"error":"old and new passwords required"})");
        }

        std::string old_password = body["old_password"].get<std::string>();
        std::string new_password = body["new_password"].get<std::string>();

        if (old_password.empty() || new_password.empty()) {
            return crow::response(400, R"({"error":"old and new passwords required"})");
        }

        if (!auth.login(user, old_password)) {
            return crow::response(401, R"({"error":"invalid old password"})");
        }

        // new_password is already client-hashed
        std::string salt = generate_salt();
        std::string hash = Auth::hash_password(new_password, salt);
        if (db.update_password(user, hash, salt)) {
            return crow::response(200, R"({"ok":true})");
        }
        return crow::response(500, R"({"error":"failed to change password"})");
    });

    // DELETE /api/users/me - delete own account
    CROW_ROUTE(app, "/api/users/me")
        .methods("DELETE"_method)
    ([&db, &auth](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        if (auth.is_admin(user)) {
            return crow::response(400, R"({"error":"admin cannot delete account"})");
        }

        if (db.delete_user(user)) {
            crow::response res(200, R"({"ok":true})");
            res.add_header("Set-Cookie", "kiftd_user=; Path=/; HttpOnly; Max-Age=0");
            return res;
        }
        return crow::response(500, R"({"error":"failed to delete account"})");
    });
}

} // namespace kiftd
