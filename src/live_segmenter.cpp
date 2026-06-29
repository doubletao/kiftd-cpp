#include "live_segmenter.h"
#include "utils/common.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <stringapiset.h>
#endif

namespace fs = std::filesystem;

namespace kiftd {

LiveSegmenter::LiveSegmenter(const Config& cfg)
    : cfg_(cfg), temp_base_dir_(cfg.data_dir + "/live_temp") {
    fs::create_directories(temp_base_dir_);
}

LiveSegmenter::~LiveSegmenter() {
    // Clean up all temp files
    std::error_code ec;
    fs::remove_all(temp_base_dir_, ec);
}

double LiveSegmenter::get_duration(const std::string& input_path) {
    {
        std::lock_guard<std::mutex> lock(duration_cache_mutex_);
        auto it = duration_cache_.find(input_path);
        if (it != duration_cache_.end()) {
            double age = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - it->second.cached_at).count();
            if (age < DURATION_CACHE_TTL_SECONDS) {
                return it->second.duration;
            }
            duration_cache_.erase(it);
        }
    }

    double dur = get_duration_uncached(input_path);
    if (dur > 0) {
        std::lock_guard<std::mutex> lock(duration_cache_mutex_);
        duration_cache_[input_path] = {dur, std::chrono::steady_clock::now()};
    }
    return dur;
}

double LiveSegmenter::get_duration_uncached(const std::string& input_path) {
    std::string cmd = "\"" + cfg_.ffprobe_path + "\" -v quiet -print_format json -show_format \"" + input_path + "\"";
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return 0;
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
    if (!ok) { CloseHandle(hRead); return 0; }
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
#else
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 0;
    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    pclose(pipe);
#endif
    auto pos = output.find("\"duration\"");
    if (pos == std::string::npos) return 0;
    auto colon = output.find(':', pos);
    if (colon == std::string::npos) return 0;
    auto quote1 = output.find('"', colon + 1);
    if (quote1 == std::string::npos) return 0;
    auto quote2 = output.find('"', quote1 + 1);
    if (quote2 == std::string::npos) return 0;
    try {
        return std::stod(output.substr(quote1 + 1, quote2 - quote1 - 1));
    } catch (...) {
        return 0;
    }
}

LiveSegmenter::TranscodeParams LiveSegmenter::get_params(const std::string& preset_name) const {
    auto it = cfg_.transcode_presets.find(preset_name);
    TranscodeParams p;
    p.resolution = 0;
    p.crf = 28;
    p.preset = "veryfast";
    if (it != cfg_.transcode_presets.end()) {
        p.resolution = it->second.resolution;
        p.crf = it->second.crf;
        p.preset = it->second.preset;
    }

    // Build video args based on profile
    if (cfg_.transcode_profile == "nvenc") {
        p.video_args = "-c:v h264_nvenc -cq " + std::to_string(p.crf) + " -preset " + p.preset;
    } else if (cfg_.transcode_profile == "qsv") {
        p.video_args = "-c:v h264_qsv -global_quality " + std::to_string(p.crf) + " -preset " + p.preset;
    } else if (cfg_.transcode_profile == "amf") {
        p.video_args = "-c:v h264_amf -qp_i " + std::to_string(p.crf) + " -qp_p " + std::to_string(p.crf) + " -quality " + p.preset;
    } else {
        p.video_args = "-c:v libx264 -crf " + std::to_string(p.crf) + " -preset " + p.preset;
    }
    p.video_args += " -c:a aac -b:a 128k";
    return p;
}

std::string LiveSegmenter::build_init_cmd(const std::string& input_path, const std::string& output_path,
                                            const TranscodeParams& params,
                                            int audio_index, int subtitle_index) const {
    std::string vf;
    if (params.resolution > 0) {
        vf += "scale=-2:" + std::to_string(params.resolution);
    }
    if (subtitle_index >= 0) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(input_path) + ":si=" + std::to_string(subtitle_index);
    }
    std::string vf_args = vf.empty() ? "" : ("-vf \"" + vf + "\" ");

    std::string audio_map = (audio_index >= 0)
        ? "-map 0:a:" + std::to_string(audio_index)
        : "-map 0:a:0?";

    std::string hw_init;
    if (cfg_.transcode_profile == "qsv") hw_init = " -init_hw_device qsv=hw:0";
    else if (cfg_.transcode_profile == "nvenc") hw_init = " -init_hw_device cuda=hw:0";
    else if (cfg_.transcode_profile == "amf") hw_init = " -init_hw_device d3d11va=hw:0";

    // Generate only the moov/init box using fragmented mp4 with zero duration
    return "\"" + cfg_.ffmpeg_path + "\" -y" + hw_init
           + " -ss 0 -i \"" + input_path + "\""
           + " " + vf_args
           + "-map 0:v:0 " + audio_map
           + " " + params.video_args
           + " -f mp4 -movflags +faststart+dash -t 0"
           + " \"" + output_path + "\"";
}

std::string LiveSegmenter::build_segment_cmd(const std::string& input_path, const std::string& output_path,
                                               double start_seconds, int duration,
                                               const TranscodeParams& params,
                                               int audio_index, int subtitle_index) const {
    std::string vf;
    if (params.resolution > 0) {
        vf += "scale=-2:" + std::to_string(params.resolution);
    }
    if (subtitle_index >= 0) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(input_path) + ":si=" + std::to_string(subtitle_index);
    }
    std::string vf_args = vf.empty() ? "" : ("-vf \"" + vf + "\" ");

    std::string audio_map = (audio_index >= 0)
        ? "-map 0:a:" + std::to_string(audio_index)
        : "-map 0:a:0?";

    std::string hw_init;
    if (cfg_.transcode_profile == "qsv") hw_init = " -init_hw_device qsv=hw:0";
    else if (cfg_.transcode_profile == "nvenc") hw_init = " -init_hw_device cuda=hw:0";
    else if (cfg_.transcode_profile == "amf") hw_init = " -init_hw_device d3d11va=hw:0";

    return "\"" + cfg_.ffmpeg_path + "\" -y" + hw_init
           + " -ss " + std::to_string(start_seconds)
           + " -i \"" + input_path + "\""
           + " " + vf_args
           + "-map 0:v:0 " + audio_map
           + " " + params.video_args
           + " -f mp4 -movflags +dash+faststart"
           + " -t " + std::to_string(duration)
           + " \"" + output_path + "\"";
}

static int run_command(const std::string& cmd) {
#ifdef _WIN32
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring wcmd = utf8_to_wide(cmd);
    std::vector<wchar_t> cmd_buf(wcmd.begin(), wcmd.end());
    cmd_buf.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr,
                              FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exit_code);
#else
    return std::system(cmd.c_str());
#endif
}

std::string LiveSegmenter::get_init_segment(const std::string& file_id, const std::string& input_path,
                                              int audio_index, int subtitle_index,
                                              const std::string& preset_name) {
    std::string cache_key = file_id + "|" + preset_name;
    {
        std::lock_guard<std::mutex> lock(init_cache_mutex_);
        auto it = init_cache_.find(cache_key);
        if (it != init_cache_.end() && fs::exists(it->second)) {
            return it->second;
        }
    }

    // Generate init.mp4 (moov box only, very fast)
    std::string init_path = temp_base_dir_ + "/" + file_id + "_init.mp4";
    auto params = get_params(preset_name);
    std::string cmd = build_init_cmd(input_path, init_path, params, audio_index, subtitle_index);

    std::cout << "[Live] generating init.mp4 for " << file_id << std::endl;
    int ret = run_command(cmd);
    if (ret != 0 || !fs::exists(init_path)) {
        std::cerr << "[Live] init.mp4 generation failed for " << file_id << " (exit " << ret << ")" << std::endl;
        return "";
    }

    {
        std::lock_guard<std::mutex> lock(init_cache_mutex_);
        init_cache_[cache_key] = init_path;
    }

    std::cout << "[Live] init.mp4 ready for " << file_id << std::endl;
    return init_path;
}

std::string LiveSegmenter::transcode_segment(const std::string& file_id, const std::string& input_path,
                                               int segment_id, int segment_length,
                                               int audio_index, int subtitle_index,
                                               const std::string& preset_name) {
    // Wait for concurrency slot
    {
        std::unique_lock<std::mutex> lock(concurrency_mutex_);
        concurrency_cv_.wait(lock, [this] { return running_count_ < MAX_CONCURRENT; });
        running_count_++;
    }

    double start_seconds = static_cast<double>(segment_id * segment_length);
    std::string seg_path = temp_base_dir_ + "/" + file_id + "_seg" + std::to_string(segment_id) + ".mp4";
    auto params = get_params(preset_name);
    std::string cmd = build_segment_cmd(input_path, seg_path, start_seconds, segment_length,
                                         params, audio_index, subtitle_index);

    std::cout << "[Live] transcoding segment " << segment_id << " for " << file_id << std::endl;
    int ret = run_command(cmd);

    // Release concurrency slot
    {
        std::lock_guard<std::mutex> lock(concurrency_mutex_);
        running_count_--;
    }
    concurrency_cv_.notify_one();

    if (ret != 0 || !fs::exists(seg_path)) {
        std::cerr << "[Live] segment " << segment_id << " transcode failed for " << file_id
                  << " (exit " << ret << ")" << std::endl;
        return "";
    }

    std::cout << "[Live] segment " << segment_id << " ready for " << file_id << std::endl;
    return seg_path;
}

void LiveSegmenter::cleanup(const std::string& file_id) {
    std::error_code ec;
    // Remove init cache entry
    {
        std::lock_guard<std::mutex> lock(init_cache_mutex_);
        for (auto it = init_cache_.begin(); it != init_cache_.end(); ) {
            if (it->first.find(file_id + "|") == 0) {
                fs::remove(it->second, ec);
                it = init_cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Remove any remaining temp files for this file_id
    std::string prefix = file_id + "_";
    for (auto& entry : fs::directory_iterator(temp_base_dir_, ec)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            if (name.find(prefix) == 0) {
                fs::remove(entry.path(), ec);
            }
        }
    }
}

} // namespace kiftd
