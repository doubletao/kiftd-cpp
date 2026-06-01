#include "controllers/file_controller.h"
#include "utils/uuid.h"
#include <nlohmann/json.hpp>
#include <fstream>
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
    // Convert to lowercase
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
    if (ext == "mp4") return "video/mp4";
    return "application/octet-stream";
}

void register_file_routes(crow::SimpleApp& app, Database& db, FileStore& store) {

    // POST /api/files/upload - upload file (multipart)
    CROW_ROUTE(app, "/api/files/upload")
        .methods("POST"_method)
    ([&db, &store](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Parse multipart
        crow::multipart::message msg(req);
        std::string folder_id;
        std::string file_data;
        std::string file_name;

        for (auto& part : msg.parts) {
            auto it = part.headers.find("Content-Disposition");
            if (it == part.headers.end()) continue;

            std::string disp = it->second.value;
            if (disp.find("name=\"folder_id\"") != std::string::npos) {
                folder_id = part.body;
            } else if (disp.find("name=\"file\"") != std::string::npos ||
                       disp.find("filename=") != std::string::npos) {
                file_data = part.body;
                // Extract filename
                auto fn_pos = disp.find("filename=\"");
                if (fn_pos != std::string::npos) {
                    fn_pos += 10;
                    auto fn_end = disp.find('"', fn_pos);
                    file_name = disp.substr(fn_pos, fn_end - fn_pos);
                }
            }
        }

        if (folder_id.empty() || file_name.empty() || file_data.empty()) {
            return crow::response(400, R"({"error":"missing folder_id or file"})");
        }

        // Check folder exists
        if (folder_id != "root") {
            auto folder = db.get_folder(folder_id);
            if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");
        }

        // Save file to disk
        std::string disk_name = store.save_from_buffer(file_data.data(), file_data.size());
        if (disk_name.empty()) {
            return crow::response(500, R"({"error":"save failed"})");
        }

        // Create DB record
        FileRecord f;
        f.id = generate_uuid();
        f.name = file_name;
        f.size = static_cast<int64_t>(file_data.size());
        f.disk_name = disk_name;
        f.folder_id = folder_id == "root" ? "" : folder_id;
        f.creator = user;

        if (!db.create_file(f)) {
            store.remove(disk_name);
            return crow::response(500, R"({"error":"db insert failed"})");
        }

        return crow::response(201, nlohmann::json{{"id", f.id}, {"name", f.name}, {"size", f.size}}.dump());
    });

    // GET /api/files/<string>/download - download file
    CROW_ROUTE(app, "/api/files/<string>/download")
        .methods("GET"_method)
    ([&db, &store](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing on disk"})");

        // Stream file
        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Disposition", "attachment; filename=\"" + file.name + "\"");
        res.add_header("Content-Type", get_content_type(file.name));
        return res;
    });

    // GET /api/files/<string>/preview - preview txt/image
    CROW_ROUTE(app, "/api/files/<string>/preview")
        .methods("GET"_method)
    ([&db, &store](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing"})");

        std::string ct = get_content_type(file.name);
        // Only allow preview for text and images
        if (ct.find("text/") == std::string::npos &&
            ct.find("image/") == std::string::npos &&
            ct != "application/json" &&
            ct != "application/javascript") {
            return crow::response(403, R"({"error":"preview not supported"})");
        }

        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Type", ct);
        return res;
    });

    // PUT /api/files/<string> - rename file
    CROW_ROUTE(app, "/api/files/<string>")
        .methods("PUT"_method)
    ([&db](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string new_name = body["name"].get<std::string>();
        if (new_name.empty()) return crow::response(400, R"({"error":"name required"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        if (!db.rename_file(file_id, new_name)) {
            return crow::response(500, R"({"error":"rename failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });

    // DELETE /api/files/<string> - delete file
    CROW_ROUTE(app, "/api/files/<string>")
        .methods("DELETE"_method)
    ([&db, &store](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string disk_name = file.disk_name;
        if (!db.delete_file(file_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }
        store.remove(disk_name);

        return crow::response(200, R"({"ok":true})");
    });
}

} // namespace kiftd
