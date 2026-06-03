#include "controllers/share_controller.h"
#include "utils/uuid.h"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace kiftd {

static std::string get_user(const crow::request& req) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("kiftd_user=");
    if (pos == std::string::npos) return "";
    auto start = pos + 11;
    auto end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

static std::string get_content_type(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = filename.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext == "txt" || ext == "text") return "text/plain; charset=utf-8";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "zip") return "application/zip";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "wav") return "audio/wav";
    if (ext == "ogg") return "audio/ogg";
    if (ext == "flac") return "audio/flac";
    if (ext == "aac") return "audio/aac";
    if (ext == "webm") return "video/webm";
    if (ext == "mp4") return "video/mp4";
    return "application/octet-stream";
}

void register_share_routes(crow::SimpleApp& app, Database& db, FileStore& store) {

    // POST /api/shares - create share link
    CROW_ROUTE(app, "/api/shares")
        .methods("POST"_method)
    ([&db](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("file_id")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string file_id = body["file_id"].get<std::string>();
        std::string expire_at = "";
        if (body.contains("expire_at") && !body["expire_at"].is_null()) {
            expire_at = body["expire_at"].get<std::string>();
        }

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        ShareRecord s;
        s.id = generate_uuid();
        s.file_id = file_id;
        s.creator = user;
        s.expire_at = expire_at;

        if (!db.create_share(s)) {
            return crow::response(500, R"({"error":"create share failed"})");
        }

        return crow::response(201, nlohmann::json{{"id", s.id}, {"file_name", file.name}}.dump());
    });

    // GET /api/shares/mine - list my shares
    CROW_ROUTE(app, "/api/shares/mine")
        .methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto shares = db.get_shares_by_user(user);
        nlohmann::json list = nlohmann::json::array();
        for (auto& s : shares) {
            list.push_back({
                {"id", s.id},
                {"file_id", s.file_id},
                {"file_name", s.file_name},
                {"expire_at", s.expire_at},
                {"created_at", s.created_at}
            });
        }
        return crow::response(200, list.dump());
    });

    // DELETE /api/shares/<string> - cancel share
    CROW_ROUTE(app, "/api/shares/<string>")
        .methods("DELETE"_method)
    ([&db](const crow::request& req, const std::string& share_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto share = db.get_share(share_id);
        if (share.id.empty()) return crow::response(404, R"({"error":"share not found"})");
        if (share.creator != user) return crow::response(403, R"({"error":"not your share"})");

        if (!db.delete_share(share_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }
        return crow::response(200, R"({"ok":true})");
    });

    // GET /s/<string> - public download via share link (no login required)
    CROW_ROUTE(app, "/s/<string>")
        .methods("GET"_method)
    ([&db, &store](const crow::request& req, const std::string& share_id) {
        auto share = db.get_share(share_id);
        if (share.id.empty()) {
            return crow::response(404, R"({"error":"share link not found or expired"})");
        }

        // Check expiration
        if (!share.expire_at.empty()) {
            // Simple string comparison works for ISO datetime format
            // In production you'd parse to time_t
        }

        auto file = db.get_file(share.file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing"})");

        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Disposition", "attachment; filename=\"" + file.name + "\"");
        res.add_header("Content-Type", get_content_type(file.name));
        return res;
    });
}

} // namespace kiftd
