#pragma once
#include <string>
#include <cstdint>
#include <fstream>

namespace kiftd {

class FileStore {
public:
    FileStore(const std::string& files_dir);

    // Save uploaded file to disk, returns disk_name (UUID.bin)
    std::string save(const std::string& source_path);
    std::string save_from_buffer(const char* data, size_t size);

    // Streaming write: move a temp file directly to the store (zero-copy)
    std::string adopt_temp_file(const std::string& temp_path);

    // Get full path to a stored file
    std::string get_path(const std::string& disk_name) const;

    // Delete a stored file
    bool remove(const std::string& disk_name);

    // Check if file exists on disk
    bool exists(const std::string& disk_name) const;

private:
    std::string files_dir_;
};

} // namespace kiftd
