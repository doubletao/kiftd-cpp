#include "controllers/folder_controller.h"
#include "utils/uuid.h"
#include "utils/common.h"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace kiftd {

void register_folder_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                            TranscodeManager& transcode_mgr, const Config& cfg,
                            const std::string& secret_key) {

    // GET /api/folders/<string> - get folder contents
    CROW_ROUTE(app, "/api/folders/<string>")
        .methods("GET"_method)
    ([&db, &secret_key](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req, secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Parse pagination params
        int page = 1, page_size = 100;
        auto url_params = req.url_params;
        auto page_str = url_params.get("page");
        auto page_size_str = url_params.get("page_size");
        if (page_str) {
            try { page = std::max(1, std::stoi(page_str)); } catch (...) {}
        }
        if (page_size_str) {
            try { page_size = std::clamp(std::stoi(page_size_str), 1, 1000); } catch (...) {}
        }

        Folder folder;
        if (folder_id == "root") {
            folder.id = "";
            folder.name = "ROOT";
        } else {
            folder = db.get_folder(folder_id);
            if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");
        }

        auto subfolders = db.get_subfolders(folder_id == "root" ? "" : folder_id);
        
        // Paginated file query
        int64_t total_files = 0;
        int offset = (page - 1) * page_size;
        auto files = db.get_files_in_folder(folder_id == "root" ? "" : folder_id, offset, page_size, total_files);

        // Build breadcrumb using recursive CTE
        nlohmann::json breadcrumb = nlohmann::json::array();
        if (folder_id != "root") {
            auto ancestors = db.get_folder_ancestors(folder_id);
            for (auto& a : ancestors) {
                breadcrumb.push_back({{"id", a.id}, {"name", a.name}});
            }
        }

        nlohmann::json folder_list = nlohmann::json::array();
        for (auto& f : subfolders) {
            folder_list.push_back({{"id", f.id}, {"name", f.name}, {"creator", f.creator}, {"created_at", f.created_at}});
        }

        nlohmann::json file_list = nlohmann::json::array();
        for (auto& f : files) {
            file_list.push_back({
                {"id", f.id}, {"name", f.name}, {"size", f.size},
                {"creator", f.creator}, {"created_at", f.created_at}
            });
        }

        int total_pages = (total_files + page_size - 1) / page_size;

        nlohmann::json result;
        result["folder"] = {{"id", folder_id == "root" ? "root" : folder.id}, {"name", folder.name}};
        result["breadcrumb"] = breadcrumb;
        result["folders"] = folder_list;
        result["files"] = file_list;
        result["pagination"] = {
            {"page", page},
            {"page_size", page_size},
            {"total_files", total_files},
            {"total_pages", total_pages}
        };
        return crow::response(200, result.dump());
    });

    // POST /api/folders - create folder
    CROW_ROUTE(app, "/api/folders")
        .methods("POST"_method)
    ([&db, &secret_key](const crow::request& req) {
        std::string user = get_user(req, secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name") || !body.contains("parent_id")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string name = body["name"].get<std::string>();
        std::string parent_id = body["parent_id"].get<std::string>();

        if (name.empty()) return crow::response(400, R"({"error":"name required"})");

        // Check parent exists
        if (parent_id != "root") {
            auto parent = db.get_folder(parent_id);
            if (parent.id.empty()) return crow::response(404, R"({"error":"parent not found"})");
        }

        // Check duplicate name
        auto siblings = db.get_subfolders(parent_id == "root" ? "" : parent_id);
        for (auto& s : siblings) {
            if (s.name == name) {
                return crow::response(409, R"({"error":"folder name already exists"})");
            }
        }

        Folder f;
        f.id = generate_uuid();
        f.name = name;
        f.parent_id = parent_id == "root" ? "" : parent_id;
        f.creator = user;

        if (!db.create_folder(f)) {
            return crow::response(500, R"({"error":"create failed"})");
        }

        return crow::response(201, nlohmann::json{{"id", f.id}, {"name", f.name}}.dump());
    });

    // PUT /api/folders/<string> - rename folder
    CROW_ROUTE(app, "/api/folders/<string>")
        .methods("PUT"_method)
    ([&db, &secret_key](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req, secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string new_name = body["name"].get<std::string>();
        if (new_name.empty()) return crow::response(400, R"({"error":"name required"})");

        auto folder = db.get_folder(folder_id);
        if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");

        if (!db.rename_folder(folder_id, new_name)) {
            return crow::response(500, R"({"error":"rename failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });

    // DELETE /api/folders/<string> - delete folder
    CROW_ROUTE(app, "/api/folders/<string>")
        .methods("DELETE"_method)
    ([&db, &store, &transcode_mgr, &cfg, &secret_key](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req, secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto folder = db.get_folder(folder_id);
        if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");

        // Collect all disk_names and file_ids before deleting DB records
        auto disk_names = db.get_all_disk_names_in_folder(folder_id);
        auto file_ids = db.get_all_file_ids_in_folder(folder_id);

        // Remove play history before delete_folder (needs subfolder records to exist)
        db.delete_all_play_history_in_folder(folder_id);

        if (!db.delete_folder(folder_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }

        // Remove physical files from disk
        for (auto& dn : disk_names) {
            store.remove(dn);
        }

        // Remove transcode caches
        for (auto& fid : file_ids) {
            transcode_mgr.cancel(fid);
            std::string cache_dir = cfg.transcode_dir + "/" + fid;
            std::error_code ec;
            fs::remove_all(fs::path(cache_dir), ec);
        }

        return crow::response(200, R"({"ok":true})");
    });
}

} // namespace kiftd
