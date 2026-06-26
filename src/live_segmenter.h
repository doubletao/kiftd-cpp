#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include "config.h"

namespace kiftd {

class LiveSegmenter {
public:
    LiveSegmenter(const Config& cfg);
    ~LiveSegmenter();

    // Get video duration (cached)
    double get_duration(const std::string& input_path);

    // Generate or return cached init.mp4 for a file+preset combination
    // Returns path to init.mp4 file, or empty string on failure
    std::string get_init_segment(const std::string& file_id, const std::string& input_path,
                                  int audio_index, int subtitle_index,
                                  const std::string& preset_name);

    // Transcode a single segment (segment_id * 4 seconds)
    // Returns path to the segment file, or empty string on failure
    // Caller is responsible for deleting the file after use
    std::string transcode_segment(const std::string& file_id, const std::string& input_path,
                                   int segment_id, int segment_length,
                                   int audio_index, int subtitle_index,
                                   const std::string& preset_name);

    // Cleanup temp files for a file_id (called on session end)
    void cleanup(const std::string& file_id);

private:
    struct TranscodeParams {
        int resolution;
        int crf;
        std::string preset;
        std::string video_args;
    };

    TranscodeParams get_params(const std::string& preset_name) const;
    std::string build_segment_cmd(const std::string& input_path, const std::string& output_path,
                                   double start_seconds, int duration,
                                   const TranscodeParams& params,
                                   int audio_index, int subtitle_index) const;
    std::string build_init_cmd(const std::string& input_path, const std::string& output_path,
                                const TranscodeParams& params,
                                int audio_index, int subtitle_index) const;
    double get_duration_uncached(const std::string& input_path);

    const Config& cfg_;
    std::string temp_base_dir_;

    // Duration cache
    mutable std::mutex duration_cache_mutex_;
    struct DurationCacheEntry {
        double duration;
        std::chrono::steady_clock::time_point cached_at;
    };
    std::map<std::string, DurationCacheEntry> duration_cache_;
    static constexpr int DURATION_CACHE_TTL_SECONDS = 300;

    // Init segment cache: key = file_id + "|" + preset_name
    mutable std::mutex init_cache_mutex_;
    std::map<std::string, std::string> init_cache_;  // key -> file path

    // Concurrency control
    std::mutex concurrency_mutex_;
    std::condition_variable concurrency_cv_;
    int running_count_ = 0;
    static constexpr int MAX_CONCURRENT = 4;
};

} // namespace kiftd
