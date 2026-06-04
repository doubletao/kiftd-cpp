#include "transcode_manager.h"
#include <filesystem>
#include <sstream>
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
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[task.file_id].status = TaskStatus::Transcoding;
        }

        run_task(task);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[task.file_id] = task;
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

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

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

std::string TranscodeManager::build_ffmpeg_cmd(const TranscodeTask& task) {
    auto it = cfg_.transcode_presets.find(task.preset);
    TranscodePreset preset;
    if (it != cfg_.transcode_presets.end()) {
        preset = it->second;
    }

    std::ostringstream cmd;
    cmd << "\"" << cfg_.ffmpeg_path << "\""
        << " -y -i \"" << task.input_path << "\"";

    // Video filter chain
    std::string vf;
    if (preset.resolution > 0) {
        vf += "scale=-2:" + std::to_string(preset.resolution);
    }
    if (!task.external_subtitle_path.empty()) {
        std::string escaped;
        for (char c : task.external_subtitle_path) {
            if (c == '\\' || c == ':' || c == '\'' || c == '[' || c == ']') {
                escaped += '\\';
            }
            escaped += c;
        }
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escaped;
    } else if (task.subtitle_index >= 0) {
        std::string escaped;
        for (char c : task.input_path) {
            if (c == '\\' || c == ':' || c == '\'' || c == '[' || c == ']') {
                escaped += '\\';
            }
            escaped += c;
        }
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escaped + ":si=" + std::to_string(task.subtitle_index);
    }

    if (!vf.empty()) {
        cmd << " -vf \"" << vf << "\"";
    }

    cmd << " -map 0:v:0";
    if (task.audio_index >= 0) {
        cmd << " -map 0:a:" << task.audio_index;
    } else {
        cmd << " -map 0:a:0?";
    }

    cmd << " -c:v libx264"
        << " -crf " << preset.crf
        << " -preset " << preset.preset
        << " -c:a aac -b:a 128k"
        << " -movflags +faststart"
        << " \"" << task.cache_path << "\"";

    return cmd.str();
}

} // namespace kiftd
