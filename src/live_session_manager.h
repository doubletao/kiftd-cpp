#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace kiftd {

class LiveSessionManager {
public:
    LiveSessionManager(const Config& cfg);
    ~LiveSessionManager();

    double get_duration(const std::string& input_path);

    bool ensure_segment(const std::string& file_id, const std::string& input_path,
                         int segment_id, int segment_length_seconds,
                         int audio_index, int subtitle_index,
                         const std::string& external_subtitle_path,
                         const std::string& preset_name);

    std::string get_segment_path(const std::string& file_id, int segment_id) const;

    void cancel_session(const std::string& file_id);
    void remove_session(const std::string& file_id);
    bool is_active(const std::string& file_id) const;

private:
    std::string build_ffmpeg_cmd(const std::string& input_path, const std::string& session_dir,
                                  double start_seconds, int segment_id, int segment_length,
                                  int audio_index, int subtitle_index,
                                  const std::string& external_subtitle_path,
                                  const std::string& preset_name);

    void start_ffmpeg_locked(const std::string& file_id, const std::string& input_path,
                              double start_seconds, int segment_id, int segment_length,
                              int audio_index, int subtitle_index,
                              const std::string& external_subtitle_path,
                              const std::string& preset_name);

    void kill_ffmpeg_locked(const std::string& file_id);

    // 获取磁盘上 >= min_index 的最大分段号, 无则返回 min_index-1
    int get_disk_progress_index(const std::string& session_dir, int min_index) const;

    // 检查 session_dir 中是否存在任何 seg*.mp4 数据分段
    bool has_any_data_segment(const std::string& session_dir) const;

    const Config& cfg_;
    std::string temp_base_dir_;

    mutable std::mutex mutex_;
    std::atomic<bool> shutting_down_{false};

    // 请求分段超过此值 × 预估进度 → 触发 ffmpeg 重启
    static constexpr int SEEK_THRESHOLD = 10;

    // 两次 ffmpeg 重启之间的最小间隔(毫秒)
    // 防止过期请求在 seek 后立即重启 ffmpeg
    static constexpr int RESTART_COOLDOWN_MS = 2000;

    struct Session {
        std::string file_id;
        std::string session_dir;

        int start_segment_id = 0;     // 当前 ffmpeg 的 -start_number
        int segment_length = 4;
        std::chrono::steady_clock::time_point ffmpeg_start_time;
        std::chrono::steady_clock::time_point last_restart_time;
        uint64_t ffmpeg_generation = 0;

        std::atomic<bool> ffmpeg_running{false};
        std::atomic<bool> cancelled{false};
#ifdef _WIN32
        HANDLE process_handle = nullptr;
#else
        int pid = -1;
#endif
    };
    std::map<std::string, std::shared_ptr<Session>> sessions_;
};

} // namespace kiftd
