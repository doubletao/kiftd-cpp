#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kiftd {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

void Config::apply_defaults() {
    db_path = data_dir + "/kiftd.db";
    files_dir = data_dir + "/files";
    if (transcode_dir.empty()) {
        transcode_dir = data_dir + "/transcode";
    }

    // Ensure directories exist
    fs::create_directories(data_dir);
    fs::create_directories(files_dir);
}

void Config::load(const std::string& config_path) {
    apply_defaults();

    if (!fs::exists(config_path)) {
        return; // Use defaults
    }

    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (j.contains("port")) port = j["port"].get<int>();
        if (j.contains("data_dir")) {
            data_dir = j["data_dir"].get<std::string>();
            transcode_dir = "";  // reset so apply_defaults recalculates
            apply_defaults();
        }
        if (j.contains("accounts") && j["accounts"].is_array()) {
            accounts.clear();
            for (auto& a : j["accounts"]) {
                if (a.contains("username") && a.contains("password")) {
                    accounts.push_back({a["username"].get<std::string>(), a["password"].get<std::string>()});
                }
            }
        }
        if (j.contains("max_attempts")) max_attempts = j["max_attempts"].get<int>();
        if (j.contains("lockout_seconds")) lockout_seconds = j["lockout_seconds"].get<int>();

        // Transcode config
        if (j.contains("ffmpeg_path")) {
            ffmpeg_path = j["ffmpeg_path"].get<std::string>();
            // Derive ffprobe path from ffmpeg_path
            std::string dir = fs::path(ffmpeg_path).parent_path().string();
            std::string stem = fs::path(ffmpeg_path).stem().string();
            if (!dir.empty()) {
                ffprobe_path = dir + "/ffprobe";
            } else {
                ffprobe_path = "ffprobe";
            }
#ifdef _WIN32
            // Keep .exe extension if ffmpeg has it
            if (ffmpeg_path.size() > 4 && ffmpeg_path.substr(ffmpeg_path.size() - 4) == ".exe") {
                if (ffprobe_path.size() < 5 || ffprobe_path.substr(ffprobe_path.size() - 4) != ".exe") {
                    ffprobe_path += ".exe";
                }
            }
#endif
        }
        if (j.contains("transcode_dir")) {
            transcode_dir = j["transcode_dir"].get<std::string>();
        }
        if (j.contains("transcode_concurrency")) {
            transcode_concurrency = j["transcode_concurrency"].get<int>();
        }
        if (j.contains("transcode_presets") && j["transcode_presets"].is_object()) {
            transcode_presets.clear();
            for (auto& [key, val] : j["transcode_presets"].items()) {
                TranscodePreset p;
                if (val.contains("resolution")) p.resolution = val["resolution"].get<int>();
                if (val.contains("crf")) p.crf = val["crf"].get<int>();
                if (val.contains("preset")) p.preset = val["preset"].get<std::string>();
                transcode_presets[key] = p;
            }
        }

        // Ensure transcode dir exists if ffmpeg is configured
        if (!ffmpeg_path.empty()) {
            fs::create_directories(transcode_dir);
        }
    } catch (...) {
        // Config parse error, use defaults
    }
}

} // namespace kiftd
