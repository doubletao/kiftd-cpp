#include "live_session_manager.h"
#include <iostream>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

namespace fs = std::filesystem;

namespace kiftd {

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}
#endif

static bool is_hw_profile(const std::string& profile_name) {
    return profile_name == "nvenc" || profile_name == "qsv" || profile_name == "amf";
}

static std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

static std::string escape_vf_path(const std::string& path) {
    std::string escaped;
    for (char c : path) {
        if (c == '\\' || c == ':' || c == '\'' || c == '[' || c == ']') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

LiveSessionManager::LiveSessionManager(const Config& cfg)
    : cfg_(cfg), temp_base_dir_(cfg.data_dir + "/live_temp") {
    fs::create_directories(temp_base_dir_);
}

LiveSessionManager::~LiveSessionManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, session] : sessions_) {
        if (!session->finished) {
#ifdef _WIN32
            if (session->process_handle) {
                TerminateProcess(session->process_handle, 1);
                CloseHandle(session->process_handle);
            }
#else
            if (session->pid > 0) kill(session->pid, SIGKILL);
#endif
        }
        std::error_code ec;
        fs::remove_all(session->session_dir, ec);
    }
    sessions_.clear();
}

void LiveSessionManager::start_session(const std::string& file_id,
                                         const std::string& input_path,
                                         int audio_index, int subtitle_index,
                                         const std::string& external_subtitle_path,
                                         const std::string& preset_name) {
    // Cancel existing session for this file
    cancel_session(file_id);

    auto session = std::make_shared<Session>();
    session->file_id = file_id;
    session->session_dir = temp_base_dir_ + "/" + file_id;

    // Clean up and recreate session directory
    std::error_code ec;
    fs::remove_all(session->session_dir, ec);
    fs::create_directories(session->session_dir);

    std::string playlist_path = session->session_dir + "/playlist.m3u8";
    std::string cmd = build_ffmpeg_cmd(input_path, session->session_dir,
                                        audio_index, subtitle_index,
                                        external_subtitle_path, preset_name);
    std::cout << "[Live] " << file_id << ": " << cmd << std::endl;

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

    if (!ok) {
        session->finished = true;
        session->error = "Failed to start ffmpeg";
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[file_id] = session;
        std::cerr << "[Live] " << file_id << ": CreateProcess failed" << std::endl;
        return;
    }

    CloseHandle(pi.hThread);
    session->process_handle = pi.hProcess;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[file_id] = session;
    }

    // Monitor thread: wait for ffmpeg to finish
    std::thread([this, file_id, session, process_handle = pi.hProcess]() {
        WaitForSingleObject(process_handle, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(process_handle, &exit_code);
        CloseHandle(process_handle);

        session->process_handle = nullptr;
        session->success = (exit_code == 0);
        if (exit_code != 0) {
            session->error = "ffmpeg exited with code " + std::to_string(exit_code);
            std::cerr << "[Live] " << file_id << " FAILED: exit code " << exit_code << std::endl;
        } else {
            std::cout << "[Live] " << file_id << " DONE" << std::endl;
        }
        session->finished = true;
    }).detach();
#else
    std::thread([this, file_id, session, cmd]() {
        int ret = std::system(cmd.c_str());
        session->success = (ret == 0);
        if (ret != 0) {
            session->error = "ffmpeg exited with code " + std::to_string(ret);
        }
        session->finished = true;
    }).detach();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[file_id] = session;
    }
#endif
}

void LiveSessionManager::cancel_session(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;

    auto& session = it->second;
    if (!session->finished) {
#ifdef _WIN32
        if (session->process_handle) {
            TerminateProcess(session->process_handle, 1);
            CloseHandle(session->process_handle);
            session->process_handle = nullptr;
        }
#else
        if (session->pid > 0) kill(session->pid, SIGKILL);
#endif
    }

    std::error_code ec;
    fs::remove_all(session->session_dir, ec);
    sessions_.erase(it);
}

bool LiveSessionManager::is_active(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return false;
    return !it->second->finished;
}

std::string LiveSessionManager::get_session_dir(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return "";
    return it->second->session_dir;
}

void LiveSessionManager::remove_session(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;

    std::error_code ec;
    fs::remove_all(it->second->session_dir, ec);
    sessions_.erase(it);
}

std::string LiveSessionManager::build_ffmpeg_cmd(const std::string& input_path,
                                                   const std::string& output_dir,
                                                   int audio_index, int subtitle_index,
                                                   const std::string& external_subtitle_path,
                                                   const std::string& preset_name) {
    auto it = cfg_.transcode_presets.find(preset_name);
    int resolution = 0;
    int crf = 28;
    std::string preset = "veryfast";
    if (it != cfg_.transcode_presets.end()) {
        resolution = it->second.resolution;
        crf = it->second.crf;
        preset = it->second.preset;
    }

    // Build video filter chain
    std::string vf;
    if (resolution > 0) {
        vf += "scale=-2:" + std::to_string(resolution);
    }
    if (!external_subtitle_path.empty()) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(external_subtitle_path);
    } else if (subtitle_index >= 0) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(input_path) + ":si=" + std::to_string(subtitle_index);
    }

    // For hardware encoders with CPU filters, wrap with format conversion and hwupload
    if (!vf.empty() && is_hw_profile(cfg_.transcode_profile)) {
        vf = "format=nv12," + vf + ",hwupload";
    }

    std::string vf_args = vf.empty() ? "" : ("-vf \"" + vf + "\"");

    // Audio map
    std::string audio_map;
    if (audio_index >= 0) {
        audio_map = "-map 0:a:" + std::to_string(audio_index);
    } else {
        audio_map = "-map 0:a:0?";
    }

    std::string playlist_path = output_dir + "/playlist.m3u8";

    // Get profile template and adapt for HLS output
    auto pit = cfg_.transcode_profiles.find(cfg_.transcode_profile);
    std::string cmd_template;
    if (pit != cfg_.transcode_profiles.end()) {
        cmd_template = pit->second.command;
    } else {
        cmd_template = "\"{ffmpeg}\" -y -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v libx264 -crf {crf} -preset {preset} -c:a aac -b:a 128k \"{output}\"";
    }

    // Replace placeholders
    std::string cmd = cmd_template;
    cmd = replace_all(cmd, "{ffmpeg}", cfg_.ffmpeg_path);
    cmd = replace_all(cmd, "{input}", input_path);
    cmd = replace_all(cmd, "{vf_args}", vf_args);
    cmd = replace_all(cmd, "{audio_map}", audio_map);
    cmd = replace_all(cmd, "{crf}", std::to_string(crf));
    cmd = replace_all(cmd, "{preset}", preset);

    // Replace output and movflags for HLS
    cmd = replace_all(cmd, "-movflags +faststart", "");
    cmd = replace_all(cmd, "\"{output}\"", "");
    cmd = replace_all(cmd, "{output}", "");

    // Trim trailing whitespace
    while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();

    // Append HLS output flags
    cmd += " -f hls"
           " -hls_time 4"
           " -hls_segment_type fmp4"
           " -hls_flags delete_segments+append_list+omit_endlist"
           " -hls_list_size 0"
           " -hls_allow_cache 0"
           " \"" + playlist_path + "\"";

    return cmd;
}

} // namespace kiftd
