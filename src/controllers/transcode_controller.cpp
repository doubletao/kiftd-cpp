#include "controllers/transcode_controller.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

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

// Run ffprobe and return stdout
static std::string run_ffprobe(const std::string& ffprobe_path, const std::string& input_path) {
    std::string cmd = "\"" + ffprobe_path + "\" -v quiet -print_format json -show_streams -show_format \"" + input_path + "\"";

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr,
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

// Scan for external subtitle files matching a video file
static nlohmann::json find_external_subtitles(const std::string& video_path) {
    nlohmann::json subs = nlohmann::json::array();
    fs::path vpath(video_path);
    std::string stem = vpath.stem().string();  // e.g. "video_name"
    fs::path dir = vpath.parent_path();

    if (!fs::exists(dir) || !fs::is_directory(dir)) return subs;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        std::string ext = entry.path().extension().string();
        // Remove leading dot
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        // Convert ext to lowercase
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        // Check if it's a subtitle extension
        bool is_sub = false;
        for (auto& se : SUBTITLE_EXTS) {
            if (ext == se) { is_sub = true; break; }
        }
        if (!is_sub) continue;

        // Check if filename starts with the video stem
        if (fname.size() <= stem.size()) continue;
        if (fname.substr(0, stem.size()) != stem) continue;
        // Must be stem.something.ext or stem.ext
        char sep = fname[stem.size()];
        if (sep != '.' && sep != ' ') continue;

        // Try to detect language from filename
        // Pattern: "name.sc.ass" -> "sc", "name.en.srt" -> "en"
        std::string lang = "";
        std::string remaining = fname.substr(stem.size());
        // remaining is like ".sc.ass" or ".en.srt"
        // Find the first dot-separated segment
        if (!remaining.empty() && remaining[0] == '.') {
            auto first_dot = 1;
            auto second_dot = remaining.find('.', first_dot);
            if (second_dot != std::string::npos) {
                std::string tag = remaining.substr(first_dot, second_dot - first_dot);
                // Common language tags
                if (tag == "sc" || tag == "chs" || tag == "zh" || tag == "zh-cn") lang = "chs";
                else if (tag == "tc" || tag == "cht" || tag == "zh-tw") lang = "cht";
                else if (tag == "en" || tag == "eng") lang = "en";
                else if (tag == "ja" || tag == "jpn") lang = "ja";
                else if (tag == "ko" || tag == "kor") lang = "ko";
                else if (tag.size() >= 2 && tag.size() <= 5) lang = tag;
            }
        }

        nlohmann::json sub;
        sub["path"] = entry.path().string();
        sub["filename"] = fname;
        sub["language"] = lang;
        sub["ext"] = ext;
        subs.push_back(sub);
    }
    return subs;
}

void register_transcode_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                                TranscodeManager& mgr, const Config& cfg) {

    // GET /api/config/transcode - return transcode config to frontend
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
        }
        return crow::response(200, j.dump());
    });

    // POST /api/files/<string>/probe - probe video streams
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

        // Extract useful stream info
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
                    // Frame rate
                    if (s.contains("r_frame_rate")) {
                        info["fps"] = s["r_frame_rate"].get<std::string>();
                    }
                } else if (codec_type == "audio") {
                    info["channels"] = s.value("channels", 0);
                    info["channel_layout"] = s.value("channel_layout", "");
                    info["sample_rate"] = s.value("sample_rate", "");
                }

                // Language tag
                std::string lang = "und";
                if (s.contains("tags") && s["tags"].contains("language")) {
                    lang = s["tags"]["language"].get<std::string>();
                }
                info["language"] = lang;

                // Title tag
                if (s.contains("tags") && s["tags"].contains("title")) {
                    info["title"] = s["tags"]["title"].get<std::string>();
                }

                result["streams"].push_back(info);
            }
        }

        // Duration
        if (probe.contains("format") && probe["format"].contains("duration")) {
            result["duration"] = probe["format"]["duration"].get<std::string>();
        }

        // External subtitles
        result["external_subtitles"] = find_external_subtitles(input_path);

        return crow::response(200, result.dump());
    });

    // POST /api/files/<string>/transcode - submit transcode task
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

        int audio_index = -1;
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

        // Validate external subtitle path exists if provided
        if (!external_subtitle_path.empty() && !fs::exists(external_subtitle_path)) {
            return crow::response(400, R"({"error":"external subtitle file not found"})");
        }

        // Use disk_name as input path (file is stored as UUID.bin, not just UUID)
        if (!mgr.submit(file_id, preset, input_path, audio_index, subtitle_index, external_subtitle_path)) {
            return crow::response(409, R"({"error":"task already exists"})");
        }

        return crow::response(201, R"({"ok":true})");
    });

    // GET /api/files/<string>/transcode/status - query task status
    CROW_ROUTE(app, "/api/files/<string>/transcode/status")
        .methods("GET"_method)
    ([&mgr](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto tasks = mgr.get_tasks_for_file(file_id);
        nlohmann::json result = nlohmann::json::array();
        for (auto& t : tasks) {
            nlohmann::json j;
            j["preset"] = t.preset;
            switch (t.status) {
                case TaskStatus::Pending:     j["status"] = "pending"; break;
                case TaskStatus::Transcoding: j["status"] = "transcoding"; break;
                case TaskStatus::Done:        j["status"] = "done"; break;
                case TaskStatus::Failed:      j["status"] = "failed"; break;
            }
            if (!t.error.empty()) j["error"] = t.error;
            result.push_back(j);
        }
        return crow::response(200, result.dump());
    });

    // DELETE /api/files/<string>/transcode - delete transcode cache
    CROW_ROUTE(app, "/api/files/<string>/transcode")
        .methods("DELETE"_method)
    ([&mgr, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Delete all presets for this file
        int deleted = 0;
        for (auto& [name, p] : cfg.transcode_presets) {
            if (mgr.delete_cache(file_id, name)) {
                deleted++;
            }
        }

        return crow::response(200, nlohmann::json{{"deleted", deleted}}.dump());
    });

    // GET /api/files/<string>/transcode/stream - stream transcoded mp4
    CROW_ROUTE(app, "/api/files/<string>/transcode/stream")
        .methods("GET"_method)
    ([&mgr, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        // Determine which preset to stream
        auto url_preset = req.url_params.get("preset");
        std::string path;
        if (url_preset) {
            path = mgr.get_output_path(file_id, url_preset);
        } else {
            // No preset specified — find first available
            for (auto& [name, p] : cfg.transcode_presets) {
                std::string candidate = mgr.get_output_path(file_id, name);
                if (fs::exists(candidate)) {
                    path = candidate;
                    break;
                }
            }
            if (path.empty()) {
                return crow::response(404, R"({"error":"no transcoded file found"})");
            }
        }
        if (!fs::exists(path)) {
            return crow::response(404, R"({"error":"transcoded file not found"})");
        }

        // Handle Range requests for video seeking
        crow::response res;
        auto range_header = req.get_header_value("Range");

        uint64_t file_size = fs::file_size(path);

        if (!range_header.empty() && range_header.find("bytes=") == 0) {
            // Parse Range: bytes=start-end
            std::string range_val = range_header.substr(6);
            auto dash_pos = range_val.find('-');
            uint64_t start = 0, end = file_size - 1;

            if (dash_pos == 0) {
                // bytes=-N (last N bytes)
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
        } else {
            res.set_static_file_info(path);
        }

        res.add_header("Content-Type", "video/mp4");
        res.add_header("Accept-Ranges", "bytes");
        return res;
    });
}

} // namespace kiftd
