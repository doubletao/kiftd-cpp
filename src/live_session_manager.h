#pragma once
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace kiftd {

class LiveSessionManager {
public:
    LiveSessionManager(const Config& cfg);
    ~LiveSessionManager();

    // Start a live transcode session. ffmpeg writes HLS segments to a temp directory.
    // Returns immediately (ffmpeg runs in background).
    void start_session(const std::string& file_id, const std::string& input_path,
                       int audio_index, int subtitle_index,
                       const std::string& external_subtitle_path,
                       const std::string& preset_name);

    // Cancel a running session (kills ffmpeg, cleans up temp dir).
    void cancel_session(const std::string& file_id);

    // Check if session is active (ffmpeg still running).
    bool is_active(const std::string& file_id) const;

    // Get the directory where HLS segments are written.
    std::string get_session_dir(const std::string& file_id) const;

    // Remove a finished session and its temp dir.
    void remove_session(const std::string& file_id);

private:
    std::string build_ffmpeg_cmd(const std::string& input_path, const std::string& output_dir,
                                  int audio_index, int subtitle_index,
                                  const std::string& external_subtitle_path,
                                  const std::string& preset_name);

    const Config& cfg_;
    std::string temp_base_dir_;

    mutable std::mutex mutex_;
    struct Session {
        std::string file_id;
        std::string session_dir;  // temp_base_dir / file_id
        std::atomic<bool> finished{false};
        std::atomic<bool> success{false};
        std::string error;
#ifdef _WIN32
        HANDLE process_handle = nullptr;
#else
        int pid = -1;
#endif
    };
    std::map<std::string, std::shared_ptr<Session>> sessions_;
};

} // namespace kiftd
