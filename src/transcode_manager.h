#pragma once
#include <string>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include "config.h"

namespace kiftd {

enum class TaskStatus { Pending, Transcoding, Done, Failed };

struct TranscodeTask {
    std::string id;           // file_id + "_" + preset
    std::string file_id;
    std::string preset;
    std::string input_path;   // absolute path to source file
    TaskStatus status = TaskStatus::Pending;
    std::string error;
    std::string output_path;
};

class TranscodeManager {
public:
    TranscodeManager(const Config& cfg, const std::string& files_dir);
    ~TranscodeManager();

    void start();
    void stop();

    // Submit a transcode task. Returns false if already exists.
    bool submit(const std::string& file_id, const std::string& preset,
                const std::string& input_path, int audio_index, int subtitle_index,
                const std::string& external_subtitle_path = "");

    // Query task status. Returns empty task if not found.
    TranscodeTask get_status(const std::string& file_id, const std::string& preset) const;

    // Get all tasks for a file
    std::vector<TranscodeTask> get_tasks_for_file(const std::string& file_id) const;

    // Delete transcode cache file and task record
    bool delete_cache(const std::string& file_id, const std::string& preset);

    // Check if cache exists for a file (any preset)
    bool has_any_cache(const std::string& file_id) const;

    // Get output path for a transcoded file
    std::string get_output_path(const std::string& file_id, const std::string& preset) const;

private:
    void worker_loop();
    void run_task(TranscodeTask& task, int audio_index, int subtitle_index,
                  const std::string& external_subtitle_path = "");
    std::string build_ffmpeg_cmd(const TranscodeTask& task, const std::string& input_path,
                                  int audio_index, int subtitle_index,
                                  const std::string& external_subtitle_path = "");

    const Config& cfg_;
    std::string files_dir_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    // queue item: task + (audio_idx, subtitle_idx, external_subtitle_path)
    struct QueueItem {
        TranscodeTask task;
        int audio_index;
        int subtitle_index;
        std::string external_subtitle_path;
    };
    std::queue<QueueItem> queue_;
    std::map<std::string, TranscodeTask> tasks_;  // id -> task
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<int> active_count_{0};
};

} // namespace kiftd
