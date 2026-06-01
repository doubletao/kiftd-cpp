#include "controllers/folder_controller.h"
#include "utils/uuid.h"
#include <nlohmann/json.hpp>

namespace kiftd {

// Helper: extract username from cookie
static std::string get_user(const crow::request& req) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("kiftd_user=");
    if (pos == std::string::npos) return "";
    auto start = pos + 11;
    auto end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void register_folder_routes(crow::SimpleApp& app, Database& db) {

    // GET /api/folders/<string> - get folder contents
    CROW_ROUTE(app, "/api/folders/<string>")
        .methods("GET"_method)
    ([&db](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        Folder folder;
        if (folder_id == "root") {
            folder.id = "";
            folder.name = "ROOT";
        } else {
            folder = db.get_folder(folder_id);
            if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");
        }

        auto subfolders = db.get_subfolders(folder_id == "root" ? "" : folder_id);
        auto files = db.get_files_in_folder(folder_id == "root" ? "" : folder_id);

        // Build breadcrumb
        nlohmann::json breadcrumb = nlohmann::json::array();
        if (folder_id != "root") {
            // Walk up parent chain
            std::vector<Folder> chain;
            Folder cur = folder;
            while (!cur.id.empty()) {
                chain.push_back(cur);
                if (cur.parent_id.empty()) break;
                cur = db.get_folder(cur.parent_id);
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                breadcrumb.push_back({{"id", it->id}, {"name", it->name}});
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

        nlohmann::json result;
        result["folder"] = {{"id", folder_id == "root" ? "root" : folder.id}, {"name", folder.name}};
        result["breadcrumb"] = breadcrumb;
        result["folders"] = folder_list;
        result["files"] = file_list;
        return crow::response(200, result.dump());
    });

    // POST /api/folders - create folder
    CROW_ROUTE(app, "/api/folders")
        .methods("POST"_method)
    ([&db](const crow::request& req) {
        std::string user = get_user(req);
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
    ([&db](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req);
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
    ([&db](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto folder = db.get_folder(folder_id);
        if (folder.id.empty()) return crow::response(404, R"({"error":"folder not found"})");

        if (!db.delete_folder(folder_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });
}

} // namespace kiftd
