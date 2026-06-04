#include "transcode_manager.h"
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <vector>
#include <stringapiset.h>
#else
#include <cstdlib>
#endif

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}
#endif

namespace fs = std::filesystem;

namespace kiftd {

TranscodeManager::TranscodeManager(const Config& cfg, const std::string& files_dir)
    : cfg_(cfg), files_dir_(files_dir) {}

TranscodeManager::~TranscodeManager() {
    stop();
}

void TranscodeManager::start() {
    if (running_) return;
    running_ = true;
    int n = cfg_.transcode_concurrency;
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) {
        workers_.emplace_back(&TranscodeManager::worker_loop, this);
    }
}

void TranscodeManager::stop() {
    running_ = false;
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

bool TranscodeManager::submit(const std::string& file_id, const std::string& input_path,
                               const std::string& cache_path, const std::string& preset,
                               int audio_index, int subtitle_index,
                               const std::string& external_subtitle_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(file_id);
    if (it != tasks_.end()) {
        auto s = it->second.status;
        if (s == TaskStatus::Pending || s == TaskStatus::Transcoding) {
            return false;
        }
        tasks_.erase(it);
    }

    TranscodeTask task;
    task.file_id = file_id;
    task.input_path = input_path;
    task.cache_path = cache_path;
    task.preset = preset;
    task.audio_index = audio_index;
    task.subtitle_index = subtitle_index;
    task.external_subtitle_path = external_subtitle_path;
    task.status = TaskStatus::Pending;

    tasks_[file_id] = task;
    queue_.push(task);
    cv_.notify_one();
    return true;
}

std::string TranscodeManager::get_status(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(file_id);
    if (it != tasks_.end()) {
        switch (it->second.status) {
            case TaskStatus::Pending: return "pending";
            case TaskStatus::Transcoding: return "transcoding";
            case TaskStatus::Done: return "done";
            case TaskStatus::Failed: return "failed";
        }
    }
    return "none";
}

bool TranscodeManager::cancel(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(file_id);
    if (it == tasks_.end()) return false;

    auto status = it->second.status;
    if (status != TaskStatus::Pending && status != TaskStatus::Transcoding) {
        return false;
    }

    std::string cache_path = it->second.cache_path;

    // Mark as cancelled — worker_loop will skip pending, run_task will clean up transcoding
    cancelled_.insert(file_id);

    // Kill ffmpeg process if running
    auto pit = processes_.find(file_id);
    if (pit != processes_.end()) {
#ifdef _WIN32
        TerminateProcess(pit->second, 1);
#else
        kill(pit->second, SIGKILL);
#endif
    }

    // Remove from tasks
    tasks_.erase(it);

    // Clean up partial cache file
    std::error_code ec;
    fs::remove(cache_path, ec);

    return true;
}

bool TranscodeManager::delete_cache(const std::string& cache_path) {
    std::error_code ec;
    return fs::remove(cache_path, ec);
}

bool TranscodeManager::cache_exists(const std::string& cache_path) {
    return fs::exists(cache_path) && fs::file_size(cache_path) > 0;
}

void TranscodeManager::worker_loop() {
    while (running_) {
        TranscodeTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) return;
            if (queue_.empty()) continue;
            task = std::move(queue_.front());
            queue_.pop();

            // Skip cancelled tasks
            if (cancelled_.count(task.file_id)) {
                cancelled_.erase(task.file_id);
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[task.file_id].status = TaskStatus::Transcoding;
        }

        run_task(task);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // If cancelled during execution, don't update task status
            if (cancelled_.count(task.file_id)) {
                cancelled_.erase(task.file_id);
            } else {
                tasks_[task.file_id] = task;
            }
        }
    }
}

void TranscodeManager::run_task(TranscodeTask& task) {
    // Ensure output directory exists
    fs::create_directories(fs::path(task.cache_path).parent_path());

    std::string cmd = build_ffmpeg_cmd(task);
    std::cout << "[Transcode] " << task.file_id << ": " << cmd << std::endl;

#ifdef _WIN32
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring wcmd = utf8_to_wide(cmd);
    std::vector<wchar_t> cmd_buf(wcmd.begin(), wcmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmd_buf.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (!ok) {
        task.status = TaskStatus::Failed;
        task.error = "Failed to start ffmpeg (CreateProcess failed)";
        return;
    }

    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        processes_[task.file_id] = pi.hProcess;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        processes_.erase(task.file_id);
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);

    if (exit_code != 0) {
        task.status = TaskStatus::Failed;
        task.error = "ffmpeg exited with code " + std::to_string(exit_code);
        std::cerr << "[Transcode] " << task.file_id << " FAILED: exit code " << exit_code << std::endl;
        std::error_code ec;
        fs::remove(task.cache_path, ec);
    } else {
        task.status = TaskStatus::Done;
        std::cout << "[Transcode] " << task.file_id << " DONE" << std::endl;
    }
#else
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        task.status = TaskStatus::Failed;
        task.error = "ffmpeg exited with code " + std::to_string(ret);
        std::error_code ec;
        fs::remove(task.cache_path, ec);
    } else {
        task.status = TaskStatus::Done;
    }
#endif
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

static std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

// Detect if profile uses hardware encoding (needs hwdownload/hwupload for CPU filters)
static bool is_hw_profile(const std::string& profile_name) {
    return profile_name == "nvenc" || profile_name == "qsv" || profile_name == "amf";
}

std::string TranscodeManager::build_ffmpeg_cmd(const TranscodeTask& task) {
    auto it = cfg_.transcode_presets.find(task.preset);
    TranscodePreset preset;
    if (it != cfg_.transcode_presets.end()) {
        preset = it->second;
    }

    // Build video filter chain
    std::string vf;
    if (preset.resolution > 0) {
        vf += "scale=-2:" + std::to_string(preset.resolution);
    }
    if (!task.external_subtitle_path.empty()) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(task.external_subtitle_path);
    } else if (task.subtitle_index >= 0) {
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escape_vf_path(task.input_path) + ":si=" + std::to_string(task.subtitle_index);
    }

    // For hardware encoders with CPU filters, wrap with format conversion and hwupload
    if (!vf.empty() && is_hw_profile(cfg_.transcode_profile)) {
        std::string hw_pre = "format=nv12,";
        std::string hw_post = ",hwupload";
        vf = hw_pre + vf + hw_post;
    }

    std::string vf_args = vf.empty() ? "" : ("-vf \"" + vf + "\"");

    // Build audio map
    std::string audio_map;
    if (task.audio_index >= 0) {
        audio_map = "-map 0:a:" + std::to_string(task.audio_index);
    } else {
        audio_map = "-map 0:a:0?";
    }

    // Get profile template
    auto pit = cfg_.transcode_profiles.find(cfg_.transcode_profile);
    std::string cmd_template;
    if (pit != cfg_.transcode_profiles.end()) {
        cmd_template = pit->second.command;
    } else {
        cmd_template = "\"{ffmpeg}\" -y -i \"{input}\" {vf_args} -map 0:v:0 {audio_map} -c:v libx264 -crf {crf} -preset {preset} -c:a aac -b:a 128k -movflags +faststart \"{output}\"";
    }

    // Substitute placeholders
    std::string cmd = cmd_template;
    cmd = replace_all(cmd, "{ffmpeg}", cfg_.ffmpeg_path);
    cmd = replace_all(cmd, "{input}", task.input_path);
    cmd = replace_all(cmd, "{output}", task.cache_path);
    cmd = replace_all(cmd, "{vf_args}", vf_args);
    cmd = replace_all(cmd, "{audio_map}", audio_map);
    cmd = replace_all(cmd, "{crf}", std::to_string(preset.crf));
    cmd = replace_all(cmd, "{preset}", preset.preset);

    return cmd;
}

} // namespace kiftd
