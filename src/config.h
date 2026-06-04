#pragma once
#include <string>
#include <vector>
#include <map>

namespace kiftd {

struct Account {
    std::string username;
    std::string password;
};

struct TranscodePreset {
    int resolution = 0;  // 0 = keep original
    int crf = 23;
    std::string preset = "fast";
};

struct TranscodeProfile {
    std::string name;
    std::string command;
};

struct Config {
    int port = 8081;
    std::string data_dir = "data";
    std::string db_path;        // data_dir + "/kiftd.db"
    std::string files_dir;      // data_dir + "/files"
    std::string web_dir;        // "web/dist"
    std::vector<Account> accounts = {{"admin", "admin"}};
    int max_attempts = 5;
    int lockout_seconds = 60;

    // Transcode
    std::string ffmpeg_path;    // empty = transcode disabled
    std::string ffprobe_path;   // derived from ffmpeg_path
    std::string transcode_dir;  // data_dir + "/transcode"
    int transcode_concurrency = 2;
    std::map<std::string, TranscodePreset> transcode_presets = {
        {"fast",   {480, 30, "veryfast"}},
        {"medium", {720, 27, "fast"}},
        {"high",   {0,   25, "fast"}}
    };
    std::string transcode_profile = "cpu";
    std::map<std::string, TranscodeProfile> transcode_profiles = {
        {"cpu",   {"CPU (libx264)",    "\"{ffmpeg}\" -y -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v libx264 -crf {crf} -preset {preset} -c:a aac -b:a 128k -movflags +faststart \"{output}\""}},
        {"nvenc", {"NVIDIA NVENC",     "\"{ffmpeg}\" -y -init_hw_device cuda=hw:0 -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v h264_nvenc -cq {crf} -preset {preset} -c:a aac -b:a 128k -movflags +faststart \"{output}\""}},
        {"qsv",   {"Intel QSV",       "\"{ffmpeg}\" -y -init_hw_device qsv=hw:0 -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v h264_qsv -global_quality {crf} -preset {preset} -c:a aac -b:a 128k -movflags +faststart \"{output}\""}},
        {"amf",   {"AMD AMF",         "\"{ffmpeg}\" -y -init_hw_device d3d11va=hw:0 -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v h264_amf -qp_i {crf} -qp_p {crf} -quality {preset} -c:a aac -b:a 128k -movflags +faststart \"{output}\""}}
    };

    static Config& instance();
    void load(const std::string& config_path);
    void apply_defaults();
};

} // namespace kiftd
