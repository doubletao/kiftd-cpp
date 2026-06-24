#include "live_session_manager.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <regex>

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

static bool is_hw_profile(const std::string& profile_name) {
    return profile_name == "nvenc" || profile_name == "qsv" || profile_name == "amf";
}

LiveSessionManager::LiveSessionManager(const Config& cfg)
    : cfg_(cfg), temp_base_dir_(cfg.data_dir + "/live_temp") {
    fs::create_directories(temp_base_dir_);
}

LiveSessionManager::~LiveSessionManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, session] : sessions_) {
        kill_ffmpeg_locked(id);
        std::error_code ec;
        fs::remove_all(session->session_dir, ec);
    }
    sessions_.clear();
}

double LiveSessionManager::get_duration(const std::string& input_path) {
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

bool LiveSessionManager::ensure_segment(const std::string& file_id,
                                          const std::string& input_path,
                                          int segment_id, int segment_length_seconds,
                                          int audio_index, int subtitle_index,
                                          const std::string& external_subtitle_path,
                                          const std::string& preset_name) {
    // --- Phase 1: Quick check without lock ---
    std::error_code ec;
    std::string seg_path = get_segment_path(file_id, segment_id);
    if (fs::exists(seg_path, ec) && fs::file_size(seg_path, ec) > 0) {
        return true;
    }

    // --- Phase 2: Decision phase (hold lock) ---
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Double-check after acquiring lock
        if (fs::exists(seg_path, ec) && fs::file_size(seg_path, ec) > 0) {
            return true;
        }

        // Ensure session exists
        auto it = sessions_.find(file_id);
        if (it == sessions_.end()) {
            auto session = std::make_shared<Session>();
            session->file_id = file_id;
            session->session_dir = temp_base_dir_ + "/" + file_id;
            fs::create_directories(session->session_dir);
            sessions_[file_id] = session;
            it = sessions_.find(file_id);
        }
        auto& session = it->second;

        // Decision logic using time-based progress estimation
        bool need_start = false;

        if (!session->ffmpeg_running) {
            need_start = true;
        } else if (segment_id == -1) {
            // init.mp4 — ffmpeg will produce it, just wait
        } else if (segment_id < session->start_segment_id) {
            // Backward seek
            need_start = true;
        } else {
            // Estimate current progress from elapsed time
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - session->ffmpeg_start_time).count();
            int estimated_current = session->start_segment_id + static_cast<int>(elapsed / session->segment_length);

            if (segment_id > estimated_current + SEEK_THRESHOLD) {
                need_start = true;
            }
        }

        if (need_start) {
            kill_ffmpeg_locked(file_id);

            double start_seconds = 0;
            int actual_start_segment = 0;
            if (segment_id >= 0) {
                start_seconds = static_cast<double>(segment_id * segment_length_seconds);
                actual_start_segment = segment_id;
            }

            session->start_segment_id = actual_start_segment;

            start_ffmpeg_locked(file_id, input_path, start_seconds, actual_start_segment,
                                segment_length_seconds, audio_index, subtitle_index,
                                external_subtitle_path, preset_name);
        }
    }
    // Lock released

    // --- Phase 3: Wait for segment to be ready (no lock held) ---
    std::string next_seg_path = get_segment_path(file_id, segment_id + 1);

    for (int i = 0; i < 300; i++) {  // up to 30 seconds
        std::error_code ec2;
        bool seg_exists = fs::exists(seg_path, ec2) && fs::file_size(seg_path, ec2) > 0;
        if (seg_exists) {
            bool next_exists = fs::exists(next_seg_path, ec2) && fs::file_size(next_seg_path, ec2) > 0;
            bool ffmpeg_done = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = sessions_.find(file_id);
                if (it != sessions_.end()) {
                    ffmpeg_done = !it->second->ffmpeg_running;
                }
            }
            if (next_exists || ffmpeg_done) {
                return true;
            }
        }

        // Fast-fail checks
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(file_id);
            if (it == sessions_.end() || it->second->cancelled) {
                return false;
            }
            // ffmpeg exited but segment still missing → transcode failure
            if (!it->second->ffmpeg_running && !seg_exists) {
                std::cerr << "[Live] " << file_id << " ffmpeg exited but segment "
                          << segment_id << " not produced" << std::endl;
                return false;
            }
            // Segment is before current ffmpeg start — another request restarted us
            if (segment_id >= 0 && segment_id < it->second->start_segment_id && !seg_exists) {
                std::cerr << "[Live] " << file_id << " segment " << segment_id
                          << " is before ffmpeg start " << it->second->start_segment_id
                          << " and doesn't exist" << std::endl;
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cerr << "[Live] " << file_id << " timeout waiting for segment " << segment_id << std::endl;
    return false;
}

std::string LiveSessionManager::get_segment_path(const std::string& file_id, int segment_id) const {
    std::string dir = temp_base_dir_ + "/" + file_id;
    if (segment_id == -1) {
        return dir + "/init.mp4";
    }
    return dir + "/seg" + std::to_string(segment_id) + ".mp4";
}

void LiveSessionManager::cancel_session(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;
    it->second->cancelled = true;
    kill_ffmpeg_locked(file_id);
    std::error_code ec;
    fs::remove_all(it->second->session_dir, ec);
    sessions_.erase(it);
}

void LiveSessionManager::remove_session(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;
    kill_ffmpeg_locked(file_id);
    std::error_code ec;
    fs::remove_all(it->second->session_dir, ec);
    sessions_.erase(it);
}

bool LiveSessionManager::is_active(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return false;
    return it->second->ffmpeg_running;
}

void LiveSessionManager::kill_ffmpeg_locked(const std::string& file_id) {
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;
    auto& session = it->second;

    if (session->ffmpeg_running) {
#ifdef _WIN32
        if (session->process_handle) {
            TerminateProcess(session->process_handle, 1);
            // Don't CloseHandle here — monitor thread owns it
            session->process_handle = nullptr;
        }
#else
        if (session->pid > 0) kill(session->pid, SIGKILL);
#endif
        session->ffmpeg_running = false;
        std::cout << "[Live] " << file_id << " ffmpeg killed" << std::endl;
    }
}

void LiveSessionManager::start_ffmpeg_locked(const std::string& file_id,
                                               const std::string& input_path,
                                               double start_seconds, int segment_id,
                                               int segment_length,
                                               int audio_index, int subtitle_index,
                                               const std::string& external_subtitle_path,
                                               const std::string& preset_name) {
    auto it = sessions_.find(file_id);
    if (it == sessions_.end()) return;
    auto& session = it->second;

    std::string session_dir = session->session_dir;
    std::string cmd = build_ffmpeg_cmd(input_path, session_dir, start_seconds,
                                        segment_id, segment_length,
                                        audio_index, subtitle_index,
                                        external_subtitle_path, preset_name);

    // Increment generation — stale monitors will see mismatch and no-op
    session->ffmpeg_generation++;
    uint64_t current_generation = session->ffmpeg_generation;

    // Record start time and segment length for progress estimation
    session->ffmpeg_start_time = std::chrono::steady_clock::now();
    session->segment_length = segment_length;

    std::cout << "[Live] " << file_id << " start ffmpeg at " << start_seconds
              << "s for segment " << segment_id
              << " (generation " << current_generation << ")" << std::endl;

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
        std::cerr << "[Live] " << file_id << " CreateProcess failed" << std::endl;
        return;
    }

    CloseHandle(pi.hThread);
    session->process_handle = pi.hProcess;
    session->ffmpeg_running = true;

    // Monitor thread — just waits for exit and updates state. No pending restart.
    std::thread([this, file_id, process_handle = pi.hProcess, current_generation]() {
        while (WaitForSingleObject(process_handle, 1000) == WAIT_TIMEOUT) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(file_id);
            if (it == sessions_.end() || it->second->cancelled) {
                TerminateProcess(process_handle, 1);
                CloseHandle(process_handle);
                return;
            }
        }

        DWORD exit_code = 0;
        GetExitCodeProcess(process_handle, &exit_code);
        CloseHandle(process_handle);

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(file_id);
        if (it != sessions_.end()) {
            auto& session = it->second;

            // Only update state if generation matches
            if (session->ffmpeg_generation == current_generation) {
                session->process_handle = nullptr;
                session->ffmpeg_running = false;
                if (exit_code != 0) {
                    std::cerr << "[Live] " << file_id << " ffmpeg exited with code " << exit_code << std::endl;
                } else {
                    std::cout << "[Live] " << file_id << " ffmpeg finished" << std::endl;
                }
            } else {
                std::cout << "[Live] " << file_id << " stale monitor (gen "
                          << current_generation << " vs current "
                          << session->ffmpeg_generation << "), ignoring" << std::endl;
            }
        }
    }).detach();
#else
    session->ffmpeg_running = true;
    uint64_t current_generation = session->ffmpeg_generation;
    std::thread([this, file_id, cmd, current_generation]() {
        int ret = std::system(cmd.c_str());
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(file_id);
        if (it != sessions_.end()) {
            auto& session = it->second;
            if (session->ffmpeg_generation == current_generation) {
                session->ffmpeg_running = false;
                if (ret != 0) {
                    std::cerr << "[Live] " << file_id << " ffmpeg exited with code " << ret << std::endl;
                }
            }
        }
    }).detach();
#endif
}

std::string LiveSessionManager::build_ffmpeg_cmd(const std::string& input_path,
                                                   const std::string& session_dir,
                                                   double start_seconds, int segment_id,
                                                   int segment_length,
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

    if (!vf.empty() && is_hw_profile(cfg_.transcode_profile)) {
        vf = "format=nv12," + vf + ",hwupload";
    }

    std::string vf_args = vf.empty() ? "" : ("-vf \"" + vf + "\"");

    std::string audio_map;
    if (audio_index >= 0) {
        audio_map = "-map 0:a:" + std::to_string(audio_index);
    } else {
        audio_map = "-map 0:a:0?";
    }

    std::string playlist_path = session_dir + "/playlist.m3u8";
    std::string seg_pattern = session_dir + "/seg%d.mp4";

    // Hardware device initialization
    std::string hw_init;
    if (cfg_.transcode_profile == "qsv") {
        hw_init = " -init_hw_device qsv=hw:0";
    } else if (cfg_.transcode_profile == "nvenc") {
        hw_init = " -init_hw_device cuda=hw:0";
    } else if (cfg_.transcode_profile == "amf") {
        hw_init = " -init_hw_device d3d11va=hw:0";
    }

    // Video encoding args based on profile
    std::string video_args;
    if (cfg_.transcode_profile == "nvenc") {
        video_args = "-c:v h264_nvenc -cq " + std::to_string(crf) + " -preset " + preset + " -c:a aac -b:a 128k";
    } else if (cfg_.transcode_profile == "qsv") {
        video_args = "-c:v h264_qsv -global_quality " + std::to_string(crf) + " -preset " + preset + " -c:a aac -b:a 128k";
    } else if (cfg_.transcode_profile == "amf") {
        video_args = "-c:v h264_amf -qp_i " + std::to_string(crf) + " -qp_p " + std::to_string(crf) + " -quality " + preset + " -c:a aac -b:a 128k";
    } else {
        video_args = "-c:v libx264 -crf " + std::to_string(crf) + " -preset " + preset + " -c:a aac -b:a 128k";
    }

    std::string cmd = "\"" + cfg_.ffmpeg_path + "\" -y"
                      + hw_init
                      + " -ss " + std::to_string(start_seconds)
                      + " -i \"" + input_path + "\""
                      + " " + vf_args
                      + " -map 0:v:0 " + audio_map
                      + " " + video_args
                      + " -f hls"
                      + " -hls_time " + std::to_string(segment_length)
                      + " -hls_segment_type fmp4"
                      + " -hls_fmp4_init_filename init.mp4"
                      + " -hls_segment_filename \"" + seg_pattern + "\""
                      + " -hls_list_size 0"
                      + " -start_number " + std::to_string(segment_id < 0 ? 0 : segment_id)
                      + " \"" + playlist_path + "\"";

    return cmd;
}

} // namespace kiftd
