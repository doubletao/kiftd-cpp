#include "controllers/play_history_controller.h"
#include "database.h"
#include "config.h"
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

void register_play_history_routes(crow::SimpleApp& app, Database& db, const Config& cfg) {

    // GET /api/play-history
    CROW_ROUTE(app, "/api/play-history")
        .methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto records = db.get_all_play_history();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : records) {
            nlohmann::json j;
            j["folder_id"] = r.folder_id;

            auto folder = db.get_folder(r.folder_id);
            j["folder_name"] = folder.name.empty() ? r.folder_id : folder.name;

            auto file = db.get_file(r.file_id);
            j["file_id"] = r.file_id;
            j["file_name"] = file.name.empty() ? r.file_id : file.name;

            j["position"] = r.position;
            j["duration"] = r.duration;
            j["updated_at"] = r.updated_at;
            if (!r.preset.empty()) {
                j["preset"] = r.preset;
                j["audio_index"] = r.audio_index;
                j["subtitle_index"] = r.subtitle_index;
                if (!r.external_subtitle_path.empty()) {
                    j["external_subtitle_path"] = r.external_subtitle_path;
                }
            }
            arr.push_back(j);
        }
        return crow::response(200, arr.dump());
    });

    // PUT /api/play-history
    CROW_ROUTE(app, "/api/play-history")
        .methods("PUT"_method)
    ([&db, &cfg](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("folder_id") || !body.contains("file_id")
            || !body.contains("position") || !body.contains("duration")) {
            return crow::response(400, R"({"error":"missing fields"})");
        }

        std::string folder_id = body["folder_id"].get<std::string>();
        std::string file_id = body["file_id"].get<std::string>();
        double position = body["position"].get<double>();
        double duration = body["duration"].get<double>();

        // Optional transcode params (present when saving from manual transcode)
        std::string preset;
        int audio_index = 0;
        int subtitle_index = -1;
        std::string external_subtitle_path;
        if (body.contains("preset")) preset = body["preset"].get<std::string>();
        if (body.contains("audio_index")) audio_index = body["audio_index"].get<int>();
        if (body.contains("subtitle_index")) subtitle_index = body["subtitle_index"].get<int>();
        if (body.contains("external_subtitle_path")) external_subtitle_path = body["external_subtitle_path"].get<std::string>();

        if (!db.upsert_play_history(folder_id, file_id, position, duration, preset, audio_index, subtitle_index, external_subtitle_path)) {
            return crow::response(500, R"({"error":"save failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });

    // DELETE /api/play-history/<string>
    CROW_ROUTE(app, "/api/play-history/<string>")
        .methods("DELETE"_method)
    ([&db](const crow::request& req, const std::string& folder_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        if (!db.delete_play_history(folder_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });
}

} // namespace kiftd
