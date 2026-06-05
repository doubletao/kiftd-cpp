#include "controllers/transcode_controller.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}
#endif

namespace kiftd {

static std::string get_user(const crow::request& req) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("kiftd_user=");
    if (pos == std::string::npos) return "";
    auto start = pos + 11;
    auto end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Build cache path: transcode_dir/{file_id}/video.mp4
static std::string build_cache_path(const Config& cfg, Database& db, const FileRecord& file) {
    return cfg.transcode_dir + "/" + file.id + "/video.mp4";
}

static std::string run_ffprobe(const std::string& ffprobe_path, const std::string& input_path) {
    std::string cmd = "\"" + ffprobe_path + "\" -v quiet -print_format json -show_streams -show_format \"" + input_path + "\"";

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring wcmd = utf8_to_wide(cmd);
    std::vector<wchar_t> cmd_buf(wcmd.begin(), wcmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr,
                              TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return "";
    }

    std::string output;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buf, bytesRead);
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
#else
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);
    return output;
#endif
}

static const std::vector<std::string> SUBTITLE_EXTS = {
    "srt", "ass", "ssa", "sub", "idx", "vtt", "sup"
};

static std::string get_ext_lower(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext;
}

static std::string detect_subtitle_lang(const std::string& fname, const std::string& stem) {
    std::string remaining = fname.substr(stem.size());
    if (!remaining.empty() && remaining[0] == '.') {
        auto second_dot = remaining.find('.', 1);
        if (second_dot != std::string::npos) {
            std::string tag = remaining.substr(1, second_dot - 1);
            if (tag == "sc" || tag == "chs" || tag == "zh" || tag == "zh-cn") return "chs";
            if (tag == "tc" || tag == "cht" || tag == "zh-tw") return "cht";
            if (tag == "en" || tag == "eng") return "en";
            if (tag == "ja" || tag == "jpn") return "ja";
            if (tag == "ko" || tag == "kor") return "ko";
            if (tag.size() >= 2 && tag.size() <= 5) return tag;
        }
    }
    return "";
}

static nlohmann::json find_external_subtitles_db(Database& db, FileStore& store,
                                                  const std::string& folder_id, const std::string& video_name) {
    nlohmann::json subs = nlohmann::json::array();

    auto dot = video_name.rfind('.');
    std::string stem = (dot != std::string::npos) ? video_name.substr(0, dot) : video_name;

    auto all_files = db.get_files_in_folder(folder_id);
    for (auto& f : all_files) {
        if (f.name == video_name) continue;

        std::string ext = get_ext_lower(f.name);
        bool is_sub = false;
        for (auto& se : SUBTITLE_EXTS) {
            if (ext == se) { is_sub = true; break; }
        }
        if (!is_sub) continue;

        if (f.name.size() <= stem.size()) continue;
        if (f.name.substr(0, stem.size()) != stem) continue;
        char sep = f.name[stem.size()];
        if (sep != '.' && sep != ' ') continue;

        std::string disk_path = store.get_path(f.disk_name);
        if (!fs::exists(disk_path)) continue;

        nlohmann::json sub;
        sub["path"] = disk_path;
        sub["filename"] = f.name;
        sub["language"] = detect_subtitle_lang(f.name, stem);
        sub["ext"] = ext;
        subs.push_back(sub);
    }
    return subs;
}

void register_transcode_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                                TranscodeManager& mgr, const Config& cfg) {

    // GET /api/config/transcode
    CROW_ROUTE(app, "/api/config/transcode")
        .methods("GET"_method)
    ([&cfg](const crow::request& req) {
        nlohmann::json j;
        j["enabled"] = !cfg.ffmpeg_path.empty();
        if (!cfg.ffmpeg_path.empty()) {
            nlohmann::json presets = nlohmann::json::object();
            for (auto& [name, p] : cfg.transcode_presets) {
                presets[name] = {
                    {"resolution", p.resolution},
                    {"crf", p.crf},
                    {"preset", p.preset}
                };
            }
            j["presets"] = presets;

            j["profile"] = cfg.transcode_profile;
            nlohmann::json profiles = nlohmann::json::object();
            for (auto& [name, p] : cfg.transcode_profiles) {
                profiles[name] = {{"name", p.name}};
            }
            j["profiles"] = profiles;
        }
        // Play history config (always returned, independent of ffmpeg)
        j["play_progress_threshold"] = cfg.play_progress_threshold;
        j["auto_transcode_next"] = cfg.auto_transcode_next;
        return crow::response(200, j.dump());
    });

    // POST /api/files/<string>/probe
    CROW_ROUTE(app, "/api/files/<string>/probe")
        .methods("POST"_method)
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        if (cfg.ffmpeg_path.empty()) {
            return crow::response(400, R"({"error":"ffmpeg not configured"})");
        }

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string input_path = store.get_path(file.disk_name);
        if (!fs::exists(input_path)) return crow::response(404, R"({"error":"file missing"})");

        std::string output = run_ffprobe(cfg.ffprobe_path, input_path);
        if (output.empty()) {
            return crow::response(500, R"({"error":"ffprobe failed"})");
        }

        auto probe = nlohmann::json::parse(output, nullptr, false);
        if (probe.is_discarded()) {
            return crow::response(500, R"({"error":"ffprobe output parse error"})");
        }

        nlohmann::json result;
        result["streams"] = nlohmann::json::array();

        if (probe.contains("streams") && probe["streams"].is_array()) {
            for (auto& s : probe["streams"]) {
                nlohmann::json info;
                info["index"] = s.value("index", 0);

                std::string codec_type = s.value("codec_type", "");
                info["type"] = codec_type;
                info["codec"] = s.value("codec_name", "unknown");

                if (codec_type == "video") {
                    info["width"] = s.value("width", 0);
                    info["height"] = s.value("height", 0);
                    if (s.contains("r_frame_rate")) {
                        info["fps"] = s["r_frame_rate"].get<std::string>();
                    }
                } else if (codec_type == "audio") {
                    info["channels"] = s.value("channels", 0);
                    info["channel_layout"] = s.value("channel_layout", "");
                    info["sample_rate"] = s.value("sample_rate", "");
                }

                std::string lang = "und";
                if (s.contains("tags") && s["tags"].contains("language")) {
                    lang = s["tags"]["language"].get<std::string>();
                }
                info["language"] = lang;

                if (s.contains("tags") && s["tags"].contains("title")) {
                    info["title"] = s["tags"]["title"].get<std::string>();
                }

                result["streams"].push_back(info);
            }
        }

        if (probe.contains("format") && probe["format"].contains("duration")) {
            result["duration"] = probe["format"]["duration"].get<std::string>();
        }

        result["external_subtitles"] = find_external_subtitles_db(db, store, file.folder_id, file.name);

        return crow::response(200, result.dump());
    });

    // POST /api/files/<string>/transcode
    CROW_ROUTE(app, "/api/files/<string>/transcode")
        .methods("POST"_method)
    ([&db, &store, &mgr, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        if (cfg.ffmpeg_path.empty()) {
            return crow::response(400, R"({"error":"ffmpeg not configured"})");
        }

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("preset")) {
            return crow::response(400, R"({"error":"missing preset"})");
        }

        std::string preset = body["preset"].get<std::string>();
        if (cfg.transcode_presets.find(preset) == cfg.transcode_presets.end()) {
            return crow::response(400, R"({"error":"invalid preset"})");
        }

        int audio_index = 0;
        int subtitle_index = -1;
        std::string external_subtitle_path;
        if (body.contains("audio_index")) audio_index = body["audio_index"].get<int>();
        if (body.contains("subtitle_index")) subtitle_index = body["subtitle_index"].get<int>();
        if (body.contains("external_subtitle_path")) {
            external_subtitle_path = body["external_subtitle_path"].get<std::string>();
        }

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string input_path = store.get_path(file.disk_name);
        if (!fs::exists(input_path)) return crow::response(404, R"({"error":"file missing"})");

        if (!external_subtitle_path.empty() && !fs::exists(external_subtitle_path)) {
            return crow::response(400, R"({"error":"external subtitle file not found"})");
        }

        std::string cache_path = build_cache_path(cfg, db, file);

        if (!mgr.submit(file_id, input_path, cache_path, preset, audio_index, subtitle_index, external_subtitle_path)) {
            return crow::response(409, R"({"error":"task already running"})");
        }

        return crow::response(201, R"({"ok":true})");
    });

    // GET /api/files/<string>/transcode/status
    CROW_ROUTE(app, "/api/files/<string>/transcode/status")
        .methods("GET"_method)
    ([&mgr, &db, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Check task status first (pending/transcoding)
        std::string status = mgr.get_status(file_id);
        if (status != "none") {
            return crow::response(200, nlohmann::json{{"status", status}}.dump());
        }

        // Check if cache file exists on disk
        auto file = db.get_file(file_id);
        if (!file.id.empty()) {
            std::string cache_path = build_cache_path(cfg, db, file);
            if (TranscodeManager::cache_exists(cache_path)) {
                return crow::response(200, nlohmann::json{{"status", "done"}}.dump());
            }
        }

        return crow::response(200, nlohmann::json{{"status", "none"}}.dump());
    });

    // DELETE /api/files/<string>/transcode
    CROW_ROUTE(app, "/api/files/<string>/transcode")
        .methods("DELETE"_method)
    ([&db, &cfg, &mgr](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Cancel active task if any (kills ffmpeg, removes from queue, cleans cache)
        bool cancelled = mgr.cancel(file_id);

        auto file = db.get_file(file_id);
        if (file.id.empty()) {
            return crow::response(200, nlohmann::json{{"deleted", cancelled}}.dump());
        }

        // Also delete cache directory in case it's a completed task
        std::string cache_path = build_cache_path(cfg, db, file);
        std::error_code ec;
        bool deleted = (fs::remove_all(fs::path(cache_path).parent_path(), ec) > 0) || cancelled;

        return crow::response(200, nlohmann::json{{"deleted", deleted}}.dump());
    });

    // GET /api/transcode/tasks
    CROW_ROUTE(app, "/api/transcode/tasks")
        .methods("GET"_method)
    ([&mgr, &db](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto tasks = mgr.get_all_tasks();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& t : tasks) {
            nlohmann::json j;
            j["file_id"] = t.file_id;

            auto file = db.get_file(t.file_id);
            j["file_name"] = file.name;

            switch (t.status) {
                case TaskStatus::Pending: j["status"] = "pending"; break;
                case TaskStatus::Transcoding: j["status"] = "transcoding"; break;
                case TaskStatus::Done: j["status"] = "done"; break;
                case TaskStatus::Failed: j["status"] = "failed"; break;
            }
            j["preset"] = t.preset;
            if (!t.error.empty()) j["error"] = t.error;
            arr.push_back(j);
        }
        return crow::response(200, arr.dump());
    });

    // PUT /api/transcode/tasks/reorder
    CROW_ROUTE(app, "/api/transcode/tasks/reorder")
        .methods("PUT"_method)
    ([&mgr](const crow::request& req) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("file_id") || !body.contains("direction")) {
            return crow::response(400, R"({"error":"missing file_id or direction"})");
        }

        std::string file_id = body["file_id"].get<std::string>();
        int direction = body["direction"].get<int>();

        if (!mgr.reorder_task(file_id, direction)) {
            return crow::response(409, R"({"error":"cannot reorder"})");
        }
        return crow::response(200, R"({"ok":true})");
    });

    // GET /api/files/<string>/transcode/stream
    CROW_ROUTE(app, "/api/files/<string>/transcode/stream")
        .methods("GET"_method)
    ([&db, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string cache_path = build_cache_path(cfg, db, file);
        if (!TranscodeManager::cache_exists(cache_path)) {
            return crow::response(404, R"({"error":"transcoded file not found"})");
        }

        // Handle Range requests for video seeking
        crow::response res;
        auto range_header = req.get_header_value("Range");
        uint64_t file_size = fs::file_size(cache_path);

        if (!range_header.empty() && range_header.find("bytes=") == 0) {
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
            std::ifstream ifs(cache_path, std::ios::binary);
            ifs.seekg(start);
            std::string buf(length, '\0');
            ifs.read(buf.data(), length);

            res.code = 206;
            res.body = buf;
            res.add_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
            res.add_header("Content-Length", std::to_string(length));
        } else {
            res.set_static_file_info(cache_path);
        }

        res.add_header("Content-Type", "video/mp4");
        res.add_header("Accept-Ranges", "bytes");
        return res;
    });
}

} // namespace kiftd
