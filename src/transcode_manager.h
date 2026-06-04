#pragma once
#include <string>
#include <map>
#include <deque>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include "config.h"

namespace kiftd {

enum class TaskStatus { Pending, Transcoding, Done, Failed };

struct TranscodeTask {
    std::string file_id;
    std::string cache_path;   // absolute path: transcode_dir/{folder_path}/{filename}.mp4
    std::string input_path;   // absolute path to source file
    std::string preset;       // fast/medium/high
    int audio_index = 0;
    int subtitle_index = -1;
    std::string external_subtitle_path;
    TaskStatus status = TaskStatus::Pending;
    std::string error;
};

class TranscodeManager {
public:
    TranscodeManager(const Config& cfg, const std::string& files_dir);
    ~TranscodeManager();

    void start();
    void stop();

    // Submit a transcode task. Returns false if already running.
    bool submit(const std::string& file_id, const std::string& input_path,
                const std::string& cache_path, const std::string& preset,
                int audio_index, int subtitle_index,
                const std::string& external_subtitle_path = "");

    // Query task status. Returns status from task map or checks disk.
    std::string get_status(const std::string& file_id) const;

    // Cancel a running/pending task. Kills ffmpeg if running, removes from queue, cleans cache.
    bool cancel(const std::string& file_id);

    // Delete transcode cache file
    bool delete_cache(const std::string& cache_path);

    // Check if cache exists on disk
    static bool cache_exists(const std::string& cache_path);

    // Get all tasks (for task list page)
    std::vector<TranscodeTask> get_all_tasks() const;

    // Move a pending task up or down in queue. direction: -1=up, 1=down
    bool reorder_task(const std::string& file_id, int direction);

private:
    void worker_loop();
    void run_task(TranscodeTask& task);
    std::string build_ffmpeg_cmd(const TranscodeTask& task);

    const Config& cfg_;
    std::string files_dir_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<TranscodeTask> queue_;
    std::map<std::string, TranscodeTask> tasks_;  // file_id -> task
    std::set<std::string> cancelled_;             // file_ids pending cancellation
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

#ifdef _WIN32
    std::map<std::string, void*> processes_;  // file_id -> HANDLE
#else
    std::map<std::string, int> processes_;    // file_id -> pid
#endif
};

} // namespace kiftd
