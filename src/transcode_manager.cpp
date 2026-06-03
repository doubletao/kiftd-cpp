#include "transcode_manager.h"
#include <filesystem>
#include <sstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
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

bool TranscodeManager::submit(const std::string& file_id, const std::string& preset,
                               const std::string& input_path, int audio_index, int subtitle_index,
                               const std::string& external_subtitle_path) {
    std::string task_id = file_id + "_" + preset;
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already exists
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        auto s = it->second.status;
        if (s == TaskStatus::Pending || s == TaskStatus::Transcoding) {
            return false;  // already running
        }
        // If done or failed, allow resubmit — remove old record
        tasks_.erase(it);
    }

    TranscodeTask task;
    task.id = task_id;
    task.file_id = file_id;
    task.preset = preset;
    task.input_path = input_path;
    task.status = TaskStatus::Pending;
    task.output_path = get_output_path(file_id, preset);

    tasks_[task_id] = task;
    QueueItem item;
    item.task = task;
    item.audio_index = audio_index;
    item.subtitle_index = subtitle_index;
    item.external_subtitle_path = external_subtitle_path;
    queue_.push(std::move(item));
    cv_.notify_one();
    return true;
}

TranscodeTask TranscodeManager::get_status(const std::string& file_id, const std::string& preset) const {
    std::string task_id = file_id + "_" + preset;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) return it->second;

    // Check if cache file exists (from previous run)
    std::string path = get_output_path(file_id, preset);
    if (fs::exists(path)) {
        TranscodeTask t;
        t.id = task_id;
        t.file_id = file_id;
        t.preset = preset;
        t.status = TaskStatus::Done;
        t.output_path = path;
        return t;
    }
    return {};
}

std::vector<TranscodeTask> TranscodeManager::get_tasks_for_file(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TranscodeTask> result;
    for (auto& [id, task] : tasks_) {
        if (task.file_id == file_id) {
            result.push_back(task);
        }
    }
    // Also check for cached files not in task map
    for (auto& [name, preset] : cfg_.transcode_presets) {
        std::string path = get_output_path(file_id, name);
        if (fs::exists(path)) {
            bool found = false;
            for (auto& t : result) {
                if (t.preset == name) { found = true; break; }
            }
            if (!found) {
                TranscodeTask t;
                t.id = file_id + "_" + name;
                t.file_id = file_id;
                t.preset = name;
                t.status = TaskStatus::Done;
                t.output_path = path;
                result.push_back(t);
            }
        }
    }
    return result;
}

bool TranscodeManager::delete_cache(const std::string& file_id, const std::string& preset) {
    std::string task_id = file_id + "_" + preset;
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        if (it->second.status == TaskStatus::Pending || it->second.status == TaskStatus::Transcoding) {
            return false;  // can't delete while running
        }
        tasks_.erase(it);
    }

    std::string path = get_output_path(file_id, preset);
    std::error_code ec;
    return fs::remove(path, ec);
}

bool TranscodeManager::has_any_cache(const std::string& file_id) const {
    for (auto& [name, preset] : cfg_.transcode_presets) {
        std::string path = get_output_path(file_id, name);
        if (fs::exists(path)) return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, task] : tasks_) {
        if (task.file_id == file_id &&
            (task.status == TaskStatus::Pending || task.status == TaskStatus::Transcoding)) {
            return true;
        }
    }
    return false;
}

std::string TranscodeManager::get_output_path(const std::string& file_id, const std::string& preset) const {
    return fs::absolute(cfg_.transcode_dir + "/" + file_id + "_" + preset + ".mp4").string();
}

void TranscodeManager::worker_loop() {
    while (running_) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) return;
            if (queue_.empty()) continue;
            item = std::move(queue_.front());
            queue_.pop();
        }

        // Update status to Transcoding
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[item.task.id].status = TaskStatus::Transcoding;
        }

        active_count_++;
        run_task(item.task, item.audio_index, item.subtitle_index, item.external_subtitle_path);
        active_count_--;

        // Update final status
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[item.task.id] = item.task;
        }
    }
}

void TranscodeManager::run_task(TranscodeTask& task, int audio_index, int subtitle_index,
                                 const std::string& external_subtitle_path) {
    // Use stored input_path (passed from controller, uses disk_name with .bin extension)
    std::string abs_input = fs::absolute(task.input_path).string();
    task.output_path = fs::absolute(task.output_path).string();

    // Ensure output directory exists
    fs::create_directories(fs::path(task.output_path).parent_path());

    // Build ffmpeg command
    std::string cmd = build_ffmpeg_cmd(task, abs_input, audio_index, subtitle_index, external_subtitle_path);
    std::cout << "[Transcode] " << task.id << ": " << cmd << std::endl;

#ifdef _WIN32
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    // CreateProcess needs a writable command line
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(
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
        std::cerr << "[Transcode] " << task.id << " FAILED: exit code " << exit_code << std::endl;
        // Clean up partial output
        std::error_code ec;
        fs::remove(task.output_path, ec);
    } else {
        task.status = TaskStatus::Done;
        std::cout << "[Transcode] " << task.id << " DONE" << std::endl;
    }
#else
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        task.status = TaskStatus::Failed;
        task.error = "ffmpeg exited with code " + std::to_string(ret);
        std::error_code ec;
        fs::remove(task.output_path, ec);
    } else {
        task.status = TaskStatus::Done;
    }
#endif
}

std::string TranscodeManager::build_ffmpeg_cmd(const TranscodeTask& task, const std::string& input_path,
                                                int audio_index, int subtitle_index,
                                                const std::string& external_subtitle_path) {
    auto it = cfg_.transcode_presets.find(task.preset);
    TranscodePreset preset;
    if (it != cfg_.transcode_presets.end()) {
        preset = it->second;
    }

    std::ostringstream cmd;
    cmd << "\"" << cfg_.ffmpeg_path << "\""
        << " -y -i \"" << input_path << "\"";

    // Video filter chain
    std::string vf;
    if (preset.resolution > 0) {
        vf += "scale=-2:" + std::to_string(preset.resolution);
    }
    if (!external_subtitle_path.empty()) {
        // Burn external subtitle file
        std::string escaped;
        for (char c : external_subtitle_path) {
            if (c == '\\' || c == ':' || c == '\'' || c == '[' || c == ']') {
                escaped += '\\';
            }
            escaped += c;
        }
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escaped;
    } else if (subtitle_index >= 0) {
        // Burn embedded subtitle using stream index
        std::string escaped;
        for (char c : input_path) {
            if (c == '\\' || c == ':' || c == '\'' || c == '[' || c == ']') {
                escaped += '\\';
            }
            escaped += c;
        }
        if (!vf.empty()) vf += ",";
        vf += "subtitles=" + escaped + ":si=" + std::to_string(subtitle_index);
    }

    if (!vf.empty()) {
        cmd << " -vf \"" << vf << "\"";
    }

    // Stream mapping
    cmd << " -map 0:v:0";
    if (audio_index >= 0) {
        cmd << " -map 0:a:" << audio_index;
    } else {
        cmd << " -map 0:a:0?";  // use first audio if available
    }

    // Encoding params
    cmd << " -c:v libx264"
        << " -crf " << preset.crf
        << " -preset " << preset.preset
        << " -c:a aac -b:a 128k"
        << " -movflags +faststart"
        << " \"" << task.output_path << "\"";

    return cmd.str();
}

} // namespace kiftd
