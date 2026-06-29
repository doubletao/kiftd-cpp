#include "controllers/file_controller.h"
#include "utils/uuid.h"
#include "utils/common.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace kiftd {

void register_file_routes(crow::SimpleApp& app, Database& db, FileStore& store, const Config& cfg) {

    // POST /api/files/upload - upload file (multipart)
    CROW_ROUTE(app, "/api/files/upload")
        .methods("POST"_method)
    ([&db, &store, &cfg](const crow::request& req) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Parse multipart
        crow::multipart::message msg(req);
        std::string folder_id;
        std::string file_name;
        std::string disk_name;
        int64_t file_size = 0;

        for (auto& part : msg.parts) {
            auto it = part.headers.find("Content-Disposition");
            if (it == part.headers.end()) continue;

            auto& params = it->second.params;
            auto name_it = params.find("name");
            if (name_it == params.end()) continue;

            if (name_it->second == "folder_id") {
                folder_id = std::string(part.body.begin(), part.body.end());
            } else if (name_it->second == "file") {
                auto fn_it = params.find("filename");
                if (fn_it != params.end()) {
                    file_name = fn_it->second;
                }
                
                // Stream to disk instead of loading into memory
                disk_name = store.save_from_buffer(part.body.data(), part.body.size());
                file_size = part.body.size();
            }
        }

        if (folder_id.empty() || file_name.empty() || disk_name.empty()) {
            return crow::response(400, R"({"error":"missing folder_id or file"})");
        }

        // Check folder exists
        if (folder_id != "root") {
            auto folder = db.get_folder(folder_id);
            if (folder.id.empty()) {
                store.remove(disk_name);
                return crow::response(404, R"({"error":"folder not found"})");
            }
        }

        // Create DB record
        FileRecord f;
        f.id = generate_uuid();
        f.name = file_name;
        f.size = file_size;
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
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
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
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing"})");

        std::string ct = get_content_type(file.name);
        // Only allow preview for text, images, audio and mp4 video
        if (ct.find("text/") == std::string::npos &&
            ct.find("image/") == std::string::npos &&
            ct.find("audio/") == std::string::npos &&
            ct != "video/mp4" &&
            ct != "application/json" &&
            ct != "application/javascript") {
            return crow::response(403, R"({"error":"preview not supported"})");
        }

        // Handle Range requests for video seeking
        if (ct == "video/mp4") {
            crow::response res;
            auto range_header = req.get_header_value("Range");
            uint64_t file_size = fs::file_size(path);

            if (!range_header.empty() && range_header.find("bytes=") == 0) {
                try {
                    std::string range_val = range_header.substr(6);
                    auto dash_pos = range_val.find('-');
                    uint64_t start = 0, end = file_size - 1;

                    if (dash_pos == 0) {
                        start = file_size - std::stoull(range_val.substr(1));
                    } else if (dash_pos == std::string::npos) {
                        start = std::stoull(range_val);
                    } else {
                        start = std::stoull(range_val.substr(0, dash_pos));
                        if (dash_pos + 1 < range_val.size()) {
                            end = std::stoull(range_val.substr(dash_pos + 1));
                        }
                    }

                    if (start >= file_size) {
                        res.code = 416;
                        res.add_header("Content-Range", "bytes */" + std::to_string(file_size));
                        return res;
                    }

                    uint64_t length = end - start + 1;
                    std::ifstream ifs(path, std::ios::binary);
                    ifs.seekg(start);
                    std::string buf(length, '\0');
                    ifs.read(buf.data(), length);

                    res.code = 206;
                    res.body = buf;
                    res.add_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
                    res.add_header("Content-Length", std::to_string(length));
                } catch (const std::exception&) {
                    res.code = 416;
                    res.add_header("Content-Range", "bytes */" + std::to_string(file_size));
                    return res;
                }
            } else {
                res.set_static_file_info(path);
            }

            res.add_header("Content-Type", ct);
            res.add_header("Accept-Ranges", "bytes");
            return res;
        }

        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Type", ct);
        return res;
    });

    // PUT /api/files/<string> - rename file
    CROW_ROUTE(app, "/api/files/<string>")
        .methods("PUT"_method)
    ([&db, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
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
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
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
